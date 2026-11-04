// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_MERGEMINING_H
#define BITCOIN_NODE_MERGEMINING_H

#include <primitives/block.h>
#include <sync.h>
#include <uint256.h>

#include <deque>
#include <memory>
#include <string>

class UniValue;

namespace node {
struct NodeContext;

/** -mergemining default: the merge mining endpoint is off unless asked for. */
inline constexpr bool DEFAULT_MERGEMINING{false};
/** HTTP path P2Pool uses for its merge mining client (merge_mining_client_json_rpc.cpp). */
inline constexpr const char* MERGEMINING_HTTP_PATH{"/json_rpc"};

/**
 * PQCH: the merge mining API of P2Pool (v4), so that Monero miners can mine PQCH at the same time
 * (whitepaper, section 13; auxpow.h for the proof).
 *
 *   merge_mining_get_chain_id        -> {chain_id}
 *   merge_mining_get_aux_block       {address, aux_hash, height, prev_id} -> {aux_blob, aux_diff, aux_hash}
 *   merge_mining_submit_solution     {aux_blob, aux_hash, blob, merkle_proof, path, seed_hash} -> {status}
 *
 * Hashes are hex of the 32 bytes in internal order (chain_id is the genesis block id, aux_hash
 * the id of the PQCH block template), as P2Pool hashes them into its merge mining tree.
 * aux_diff is the PQCH target as a Monero difficulty (2^256 / target).
 *
 * P2Pool's client sends no credentials, so the endpoint is unauthenticated; it only builds and
 * submits blocks paying the address it is given, and it listens where the RPC server does
 * (-rpcbind, -rpcallowip: localhost by default).
 */
class MergeMiningServer
{
public:
    explicit MergeMiningServer(NodeContext& node) : m_node{node} {}

    /** Handle one JSON-RPC 2.0 request body; returns the response body. */
    std::string HandleRequest(const std::string& body);

    UniValue GetChainId();
    UniValue GetAuxBlock(const UniValue& params);
    UniValue SubmitSolution(const UniValue& params);

private:
    struct Job {
        std::shared_ptr<const CBlock> block; //!< template, auxpow bit set, no proof yet
        uint256 hash;
        uint256 prev;
        std::string address;
        int64_t created{0}; //!< seconds
    };

    NodeContext& m_node;
    Mutex m_mutex;
    std::deque<Job> m_jobs GUARDED_BY(m_mutex); //!< most recent first

    std::shared_ptr<const CBlock> NewTemplate(const std::string& address);
};

/** Register the HTTP handler (when -mergemining is set). Needs the HTTP server. */
void StartMergeMiningRPC(NodeContext& node);
void StopMergeMiningRPC();

} // namespace node

#endif // BITCOIN_NODE_MERGEMINING_H
