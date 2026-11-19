// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>
#include <chain.h>
#include <chainparams.h>
#include <headerssync.h>
#include <pow.h>
#include <pow_randomx.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

using namespace auxpow;

namespace {

uint256 Tagged(uint8_t tag)
{
    uint256 r;
    for (size_t i = 0; i < 32; ++i) r.begin()[i] = static_cast<unsigned char>(tag + 7 * i);
    return r;
}

/** A merged mining proof for `id` from a synthetic Monero block (see pqch_auxpow_tests). */
std::shared_ptr<CAuxPow> MakeAuxPow(const uint256& id, const uint256& seed, uint32_t monero_nonce)
{
    std::vector<unsigned char> b;
    WriteMoneroVarInt(b, 16);
    WriteMoneroVarInt(b, 16);
    WriteMoneroVarInt(b, 1'790'000'000);
    const uint256 prev{Tagged(0x50)};
    b.insert(b.end(), prev.begin(), prev.end());
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(monero_nonce >> (8 * i)));
    WriteMoneroVarInt(b, 2);
    WriteMoneroVarInt(b, 3'000'060);
    WriteMoneroVarInt(b, 1);
    b.push_back(0xff);
    WriteMoneroVarInt(b, 3'000'000);
    WriteMoneroVarInt(b, 1);
    WriteMoneroVarInt(b, 600'000'000'000ULL);
    b.push_back(0x03);
    const uint256 key{Tagged(1)};
    b.insert(b.end(), key.begin(), key.end());
    b.push_back(0);
    std::vector<unsigned char> extra{0x03};
    std::vector<unsigned char> body;
    WriteMoneroVarInt(body, EncodeMergeMiningParams(1, 0));
    body.insert(body.end(), id.begin(), id.end()); // one chain: the root is the PQCH block id
    WriteMoneroVarInt(extra, body.size());
    extra.insert(extra.end(), body.begin(), body.end());
    WriteMoneroVarInt(b, extra.size());
    b.insert(b.end(), extra.begin(), extra.end());
    b.push_back(0x00);
    WriteMoneroVarInt(b, 0);
    auto a{std::make_shared<CAuxPow>()};
    CAuxPowCoinbaseHead head;
    std::string error;
    Assert(BuildFromMoneroBlock(b, seed, {}, *a, head, error));
    return a;
}

struct PresyncSetup : public TestChain100Setup {
    const Consensus::Params& params{m_node.chainman->GetConsensus()};
    const CBlockIndex* tip{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip())};

    // A clock a day after the tip, so that the test chains are not in the future.
    PresyncSetup() { SetMockTime(std::chrono::seconds{tip->GetBlockTime() + 86400}); }

    HeadersSyncState State(uint32_t sample_period, arith_uint256 min_work = ~arith_uint256{0})
    {
        return {/*id=*/0, params,
                HeadersSyncParams{.commitment_period = 10, .redownload_buffer_size = 100, .pow_sample_period = sample_period},
                *tip, min_work};
    }

    /** Next header on `prev` (at height `height - 1`), with the ASERT target; mined unless `fake`. */
    CBlockHeader Next(const CBlockHeader& prev, int height, bool fake = false, std::optional<uint256> aux_seed = std::nullopt, int64_t dt = 60)
    {
        CBlockHeader h;
        h.nVersion = 0x20000000 | (aux_seed ? VERSION_AUXPOW : 0);
        h.hashPrevBlock = prev.GetHash();
        h.hashMerkleRoot = Tagged(static_cast<uint8_t>(height));
        h.nTime = prev.nTime + dt;
        h.nBits = params.fPowUseASERT ? ASERTWorkRequiredAfter(height - 1, prev.GetBlockTime(), GetASERTAnchor(*tip, params), params) : prev.nBits;
        const uint256 seed{tip->GetAncestor(GetRandomXSeedHeight(height, params))->GetBlockHash()};
        for (uint32_t n = 0;; ++n) {
            h.nNonce = n;
            if (aux_seed) h.auxpow = MakeAuxPow(h.GetHash(), *aux_seed, n);
            std::string error;
            if (CheckHeaderProofOfWork(h, height, seed, params, error) != fake) return h;
        }
    }

    std::vector<CBlockHeader> Chain(size_t n, size_t fake_from = SIZE_MAX)
    {
        std::vector<CBlockHeader> out;
        CBlockHeader prev{tip->GetBlockHeader()};
        for (size_t i = 0; i < n; ++i) {
            out.push_back(Next(prev, tip->nHeight + 1 + i, i >= fake_from));
            prev = out.back();
        }
        return out;
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_presync_tests, PresyncSetup)

BOOST_AUTO_TEST_CASE(presync_accepts_honest_chain)
{
    const auto headers{Chain(40)};
    auto state{State(/*sample_period=*/1)};
    const auto r{state.ProcessNextHeaders(headers, /*full_headers_message=*/true)};
    BOOST_CHECK(r.success);
    BOOST_CHECK(r.request_more);
    BOOST_CHECK_EQUAL(state.GetPresyncHeight(), tip->nHeight + 40);
}

BOOST_AUTO_TEST_CASE(presync_rejects_bad_times)
{
    // A timestamp at or below the median of the last 11.
    {
        auto headers{Chain(3)};
        CBlockHeader back{Next(headers.back(), tip->nHeight + 4, false, std::nullopt, /*dt=*/-100000)};
        headers.push_back(back);
        auto state{State(1)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
    }
    // A timestamp too far in the future.
    {
        auto headers{Chain(2)};
        const int64_t future{TicksSinceEpoch<std::chrono::seconds>(NodeClock::now()) + MAX_FUTURE_BLOCK_TIME + 3600 - headers.back().GetBlockTime()};
        headers.push_back(Next(headers.back(), tip->nHeight + 3, false, std::nullopt, future));
        auto state{State(1)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
    }
}

BOOST_AUTO_TEST_CASE(presync_checks_proof_of_work)
{
    // Every header checked: one fake header ends the sync.
    {
        const auto headers{Chain(10, /*fake_from=*/7)};
        auto state{State(1)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
    }
    // Sampled (1 in 64): a long chain of fake headers is caught (miss probability (63/64)^1000 < 2e-7;
    // with 400 headers the 2e-3 chance did show up in CI).
    {
        const auto headers{Chain(1000, /*fake_from=*/0)};
        auto state{State(64)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
    }
    // The header that reaches the minimum work is always checked.
    {
        const auto headers{Chain(3, /*fake_from=*/2)};
        const arith_uint256 min_work{tip->nChainWork + GetBlockProof(headers[0]) + GetBlockProof(headers[1]) + 1};
        auto state{State(/*sample_period=*/1'000'000, min_work)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
        // The same chain with an honest last header reaches the work and moves to redownload.
        auto honest{Chain(3)};
        auto state2{State(1'000'000, tip->nChainWork + GetBlockProof(honest[0]) + GetBlockProof(honest[1]) + 1)};
        BOOST_CHECK(state2.ProcessNextHeaders(honest, true).success);
        BOOST_CHECK(state2.GetState() == HeadersSyncState::State::REDOWNLOAD);
    }
}

BOOST_AUTO_TEST_CASE(presync_merged_mined_headers)
{
    // A valid merged mined header passes; a broken proof does not.
    CBlockHeader prev{tip->GetBlockHeader()};
    const CBlockHeader good{Next(prev, tip->nHeight + 1, false, Tagged(0xa1))};
    {
        auto state{State(1)};
        BOOST_CHECK(state.ProcessNextHeaders(std::vector{good}, true).success);
    }
    {
        CBlockHeader bad{good};
        auto proof{std::make_shared<CAuxPow>(*good.auxpow)};
        proof->coinbase_extra.back() ^= 1; // the merge mining tag no longer commits to the id
        bad.auxpow = proof;
        auto state{State(1'000'000)};
        BOOST_CHECK(!state.ProcessNextHeaders(std::vector{bad}, true).success);
    }
    // The Monero key rule: at most nAuxNewSeedsPerWindow new keys, then the sync ends before any
    // RandomX cache is built for the next key.
    std::vector<CBlockHeader> headers;
    for (int i = 0; i <= params.nAuxNewSeedsPerWindow; ++i) {
        headers.push_back(Next(prev, tip->nHeight + 1 + i, false, Tagged(static_cast<uint8_t>(0xb0 + i))));
        prev = headers.back();
    }
    {
        auto state{State(1'000'000)};
        const std::vector<CBlockHeader> allowed(headers.begin(), headers.end() - 1);
        BOOST_CHECK(state.ProcessNextHeaders(allowed, true).success);
    }
    {
        auto state{State(1'000'000)};
        BOOST_CHECK(!state.ProcessNextHeaders(headers, true).success);
    }
}

BOOST_AUTO_TEST_SUITE_END()

/** The exact ASERT target, on the main network's parameters (regtest has a fixed target). */
BOOST_FIXTURE_TEST_SUITE(pqch_presync_asert_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(presync_checks_asert_target)
{
    const Consensus::Params& params{Params().GetConsensus()};
    BOOST_REQUIRE(params.fPowUseASERT);
    const CBlockHeader genesis{Params().GenesisBlock()};
    const uint256 genesis_hash{genesis.GetHash()};
    CBlockIndex start{genesis};
    start.phashBlock = &genesis_hash;
    start.nHeight = 0;
    start.nChainWork = GetBlockProof(start);

    const ASERTAnchor anchor{GetASERTAnchor(start, params)};
    std::vector<CBlockHeader> headers;
    CBlockHeader prev{genesis};
    for (int i = 1; i <= 20; ++i) {
        CBlockHeader h;
        h.nVersion = 0x20000000;
        h.hashPrevBlock = prev.GetHash();
        h.nTime = prev.nTime + (i % 2 ? 5 : 20); // fast, irregular blocks move the target
        h.nBits = ASERTWorkRequiredAfter(i - 1, prev.GetBlockTime(), anchor, params);
        headers.push_back(h);
        prev = h;
    }
    BOOST_CHECK(headers.back().nBits != genesis.nBits);
    const auto state{[&] {
        // Proof of work not sampled here (only the targets are under test).
        return HeadersSyncState{0, params, HeadersSyncParams{.commitment_period = 10, .redownload_buffer_size = 100, .pow_sample_period = 4'000'000'000U},
                                start, ~arith_uint256{0}};
    }};
    {
        auto s{state()};
        BOOST_CHECK(s.ProcessNextHeaders(headers, true).success);
    }
    for (const int delta : {-1, +1}) {
        auto bad{headers};
        bad[12].nBits += delta;
        auto s{state()};
        BOOST_CHECK(!s.ProcessNextHeaders(bad, true).success);
    }
}

BOOST_AUTO_TEST_SUITE_END()
