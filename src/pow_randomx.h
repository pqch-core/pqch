// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_RANDOMX_H
#define BITCOIN_POW_RANDOMX_H

#include <consensus/params.h>
#include <uint256.h>

#include <atomic>
#include <string>
#include <memory>
#include <span>

class CBlockHeader;
class CBlockIndex;

/**
 * PQCH: RandomX (v2) proof of work.
 *
 * The block id stays SHA256d(header), exactly as in Bitcoin, so indexes, wallets and
 * Lightning are unaffected. The proof of work is a separate hash:
 *
 *     RandomX_v2(key = seed hash, input = 80-byte serialized header) <= target(nBits)
 *
 * The key (seed) is the hash of an earlier block, so it can only be checked with the chain
 * as context: the check happens in ContextualCheckBlockHeader, not in CheckBlockHeader.
 *
 * Seed schedule (same rule as Monero, with PQCH constants):
 *   seed height = 0 (genesis)                                  if height <= interval + lag
 *               = (height - lag - 1) rounded down to a multiple of interval   otherwise
 * With interval = 4096 and lag = 64 the key changes about every 2.8 days and a reorg of
 * fewer than 64 blocks can never change it.
 */

/** Height of the block whose hash is the RandomX key for a block at `height`. */
int GetRandomXSeedHeight(int height, const Consensus::Params& params);

/** RandomX key for the block that extends pindexPrev. */
uint256 GetRandomXSeedHash(const CBlockIndex* pindexPrev, const Consensus::Params& params);

/** RandomX v2 hash of arbitrary data under a key, in light mode (256 MiB cache). Thread-safe. */
uint256 RandomXHash(std::span<const unsigned char> key, std::span<const unsigned char> input);

/** RandomX v2 proof-of-work hash of a block header under the given seed. Light mode, thread-safe. */
uint256 GetRandomXPowHash(const CBlockHeader& header, const uint256& seed);

/**
 * Full proof-of-work check of a header that extends pindexPrev: RandomX when
 * params.fPowUseRandomX is set, SHA256d (as in Bitcoin) otherwise.
 */
bool CheckBlockProofOfWork(const CBlockHeader& header, const CBlockIndex* pindexPrev, const Consensus::Params& params);
/**
 * PQCH: same, with the reason on failure ("high-hash", "bad-auxpow (...)"). Headers with the auxpow
 * bit are checked as merged mined with Monero: the work is RandomX(Monero key, Monero hashing
 * blob) and must commit to this header's id (auxpow.h). The Monero key rules need the chain and are
 * checked in validation.cpp.
 */
bool CheckBlockProofOfWork(const CBlockHeader& header, const CBlockIndex* pindexPrev, const Consensus::Params& params, std::string& error);

/**
 * PQCH: Monero key rule for a merged mined header at pindexPrev->nHeight + 1 using `seed`
 * (Consensus::Params::nAuxSeedWindow). Sets `is_new` when no block of the window on this chain
 * introduced the key. Fails ("auxpow-too-many-seeds") if a new key would exceed the window's limit.
 * Cheap (follows CBlockIndex::m_last_new_aux_seed links, at most nAuxNewSeedsPerWindow steps), so it
 * runs before any RandomX work.
 */
bool CheckAuxSeed(const uint256& seed, const CBlockIndex* pindexPrev, const Consensus::Params& params, bool& is_new, std::string& error);

/** PQCH: set pindex->m_last_new_aux_seed from its parent (call when the index is created or loaded). */
void SetLastNewAuxSeed(CBlockIndex& pindex, const Consensus::Params& params);

/**
 * Hasher for mining. In fast mode it builds the full 2 GiB dataset once per seed (slow to start,
 * several times faster per hash); otherwise it shares the light-mode verification cache.
 * Not thread-safe: use one instance per mining thread.
 */
class RandomXMiner
{
public:
    explicit RandomXMiner(bool fast_mode);
    ~RandomXMiner();
    RandomXMiner(const RandomXMiner&) = delete;
    RandomXMiner& operator=(const RandomXMiner&) = delete;

    uint256 Hash(const CBlockHeader& header, const uint256& seed);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/** Set from -randomxfastmining: whether the built-in miner (generate* RPCs) uses fast mode. */
extern std::atomic<bool> g_randomx_fast_mining;

#endif // BITCOIN_POW_RANDOMX_H
