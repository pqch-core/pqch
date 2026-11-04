// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/mergemining.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <httpserver.h>
#include <interfaces/mining.h>
#include <key_io.h>
#include <logging.h>
#include <node/context.h>
#include <node/mining_types.h>
#include <pow_randomx.h>
#include <rpc/server.h>
#include <script/script.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <validation.h>

#include <limits>
#include <stdexcept>

namespace node {
namespace {

constexpr size_t MAX_JOBS{16};      //!< templates kept for late solutions
constexpr int64_t JOB_REFRESH_S{30}; //!< new template (new transactions) after this long

std::string HashHex(const uint256& h) { return HexStr(h); }

uint256 ParseHash(const UniValue& v, const char* name)
{
    if (!v.isStr()) throw std::runtime_error(std::string{"missing "} + name);
    const auto bytes{TryParseHex<unsigned char>(v.get_str())};
    if (!bytes || bytes->size() != 32) throw std::runtime_error(std::string{"invalid "} + name);
    uint256 h;
    std::copy(bytes->begin(), bytes->end(), h.begin());
    return h;
}

/** Monero difficulty of a PQCH target: a hash h meets it when h * difficulty < 2^256. */
uint64_t TargetToDifficulty(uint32_t nBits)
{
    bool negative, overflow;
    arith_uint256 target;
    target.SetCompact(nBits, &negative, &overflow);
    if (negative || overflow || target == 0) return std::numeric_limits<uint64_t>::max();
    const arith_uint256 diff{(~arith_uint256{0}) / target};
    if (diff > arith_uint256{std::numeric_limits<uint64_t>::max()}) return std::numeric_limits<uint64_t>::max();
    return std::max<uint64_t>(1, diff.GetLow64());
}

std::unique_ptr<MergeMiningServer> g_server;

} // namespace

std::shared_ptr<const CBlock> MergeMiningServer::NewTemplate(const std::string& address)
{
    const CTxDestination dest{DecodeDestination(address)};
    if (!IsValidDestination(dest)) throw std::runtime_error("invalid PQCH address");
    const CScript script{GetScriptForDestination(dest)};
    if (!Consensus::IsPQAllowedOutput(script)) throw std::runtime_error("the address must be a P2MR (post-quantum) address");
    if (!m_node.mining) throw std::runtime_error("mining is not available");

    BlockCreateOptions options;
    options.coinbase_output_script = script;
    auto tmpl{m_node.mining->createNewBlock(options, /*cooldown=*/false)};
    if (!tmpl) throw std::runtime_error("node is shutting down");
    auto block{std::make_shared<CBlock>(tmpl->getBlock())};
    block->nVersion |= auxpow::VERSION_AUXPOW;
    block->nNonce = 0;
    block->hashMerkleRoot = BlockMerkleRoot(*block);
    return block;
}

UniValue MergeMiningServer::GetChainId()
{
    UniValue r{UniValue::VOBJ};
    r.pushKV("chain_id", HashHex(Params().GetConsensus().hashGenesisBlock));
    return r;
}

UniValue MergeMiningServer::GetAuxBlock(const UniValue& params)
{
    if (!params.isObject() || !params["address"].isStr()) throw std::runtime_error("missing address");
    const std::string address{params["address"].get_str()};
    const uint256 tip{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash())};
    const int64_t now{TicksSinceEpoch<std::chrono::seconds>(NodeClock::now())};

    Job job;
    {
        LOCK(m_mutex);
        if (!m_jobs.empty() && m_jobs.front().prev == tip && m_jobs.front().address == address &&
            now - m_jobs.front().created < JOB_REFRESH_S) {
            job = m_jobs.front();
        }
    }
    if (!job.block) {
        job.block = NewTemplate(address);
        job.hash = job.block->GetHash();
        job.prev = job.block->hashPrevBlock;
        job.address = address;
        job.created = now;
        LOCK(m_mutex);
        m_jobs.push_front(job);
        if (m_jobs.size() > MAX_JOBS) m_jobs.pop_back();
        LogInfo("Merge mining: new PQCH block template %s at height after %s\n", job.hash.ToString(), job.prev.ToString());
    }

    UniValue r{UniValue::VOBJ};
    r.pushKV("aux_blob", HexStr(job.block->SerializePure()));
    r.pushKV("aux_diff", TargetToDifficulty(job.block->nBits));
    r.pushKV("aux_hash", HashHex(job.hash));
    return r;
}

UniValue MergeMiningServer::SubmitSolution(const UniValue& params)
{
    if (!params.isObject()) throw std::runtime_error("missing parameters");
    const uint256 aux_hash{ParseHash(params["aux_hash"], "aux_hash")};
    const uint256 seed{ParseHash(params["seed_hash"], "seed_hash")};
    if (!params["blob"].isStr()) throw std::runtime_error("missing blob");
    const auto blob{TryParseHex<unsigned char>(params["blob"].get_str())};
    if (!blob) throw std::runtime_error("invalid blob");
    std::vector<uint256> aux_branch;
    if (params.exists("merkle_proof")) {
        const UniValue& proof{params["merkle_proof"]};
        if (!proof.isArray() || proof.size() > auxpow::MAX_AUX_BRANCH) throw std::runtime_error("invalid merkle_proof");
        for (size_t i = 0; i < proof.size(); ++i) aux_branch.push_back(ParseHash(proof[i], "merkle_proof"));
    }
    // "path" is not needed: the position follows from the chain id and the tag's nonce (auxpow.h).

    std::shared_ptr<const CBlock> tmpl;
    {
        LOCK(m_mutex);
        for (const Job& j : m_jobs) {
            if (j.hash == aux_hash) tmpl = j.block;
        }
    }
    if (!tmpl) throw std::runtime_error("unknown or stale aux_hash");

    auto block{std::make_shared<CBlock>(*tmpl)};
    auto proof{std::make_shared<auxpow::CAuxPow>()};
    std::string error;
    if (!auxpow::BuildFromMoneroBlock(*blob, seed, aux_branch, *proof, block->m_auxpow_head, error)) {
        throw std::runtime_error("bad Monero block: " + error);
    }
    block->auxpow = proof;

    {
        LOCK(cs_main);
        const CBlockIndex* prev{m_node.chainman->m_blockman.LookupBlockIndex(block->hashPrevBlock)};
        if (!prev) throw std::runtime_error("template's parent is unknown");
        if (!CheckBlockProofOfWork(*block, prev, m_node.chainman->GetConsensus(), error)) {
            throw std::runtime_error("rejected: " + error);
        }
    }

    bool new_block{false};
    const bool ok{m_node.chainman->ProcessNewBlock(block, /*force_processing=*/true, /*min_pow_checked=*/true, &new_block)};
    const uint256 id{block->GetHash()};
    LOCK(cs_main);
    const CBlockIndex* idx{m_node.chainman->m_blockman.LookupBlockIndex(id)};
    if (!ok || !idx || (idx->nStatus & BLOCK_FAILED_VALID) || !(idx->nStatus & BLOCK_HAVE_DATA)) {
        throw std::runtime_error("rejected (see debug.log)");
    }
    LogInfo("Merge mining: PQCH block %s accepted (Monero key %s)\n", id.ToString(), HashHex(seed));
    UniValue r{UniValue::VOBJ};
    r.pushKV("status", m_node.chainman->ActiveChain().Tip() == idx ? "accepted" : "accepted (not on the active chain)");
    return r;
}

std::string MergeMiningServer::HandleRequest(const std::string& body)
{
    UniValue request;
    UniValue reply{UniValue::VOBJ};
    reply.pushKV("jsonrpc", "2.0");
    if (!request.read(body) || !request.isObject()) {
        reply.pushKV("id", NullUniValue);
        UniValue err{UniValue::VOBJ};
        err.pushKV("code", -32700);
        err.pushKV("message", "parse error");
        reply.pushKV("error", err);
        return reply.write() + "\n";
    }
    reply.pushKV("id", request["id"]);
    try {
        if (RPCIsInWarmup(nullptr) || !m_node.chainman) throw std::runtime_error("node is starting up");
        const UniValue& method{request["method"]};
        if (!method.isStr()) throw std::runtime_error("missing method");
        const UniValue& params{request["params"]};
        if (method.get_str() == "merge_mining_get_chain_id") {
            reply.pushKV("result", GetChainId());
        } else if (method.get_str() == "merge_mining_get_aux_block") {
            reply.pushKV("result", GetAuxBlock(params));
        } else if (method.get_str() == "merge_mining_submit_solution") {
            reply.pushKV("result", SubmitSolution(params));
        } else {
            throw std::runtime_error("unknown method");
        }
    } catch (const std::exception& e) {
        UniValue err{UniValue::VOBJ};
        err.pushKV("code", -1);
        err.pushKV("message", e.what());
        reply.pushKV("error", err);
        LogDebug(BCLog::RPC, "Merge mining request failed: %s\n", e.what());
    }
    return reply.write() + "\n";
}

void StartMergeMiningRPC(NodeContext& node)
{
    g_server = std::make_unique<MergeMiningServer>(node);
    RegisterHTTPHandler(MERGEMINING_HTTP_PATH, /*exactMatch=*/true, [](HTTPRequest* req, const std::string&) {
        if (req->GetRequestMethod() != HTTPRequestMethod::POST) {
            req->WriteReply(HTTP_BAD_METHOD, "merge mining API handles only POST requests");
            return;
        }
        const std::string reply{g_server->HandleRequest(req->ReadBody())};
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, reply);
    });
    LogInfo("Merge mining API (P2Pool) enabled at %s\n", MERGEMINING_HTTP_PATH);
}

void StopMergeMiningRPC()
{
    if (!g_server) return;
    // The server object stays until exit: a request may still be running in an HTTP worker.
    UnregisterHTTPHandler(MERGEMINING_HTTP_PATH, /*exactMatch=*/true);
}

} // namespace node
