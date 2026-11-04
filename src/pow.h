// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <cstdint>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/**
 * PQCH: ASERT (aserti3) target computation, in integer arithmetic only.
 *
 * Approximates  target = refTarget * 2^((nTimeDiff - nPowTargetSpacing * (nHeightDiff + 1)) / nHalfLife)
 *
 * @param[in] refTarget          target of the anchor block (0 < refTarget <= powLimit)
 * @param[in] nPowTargetSpacing  ideal block interval in seconds
 * @param[in] nTimeDiff          time of the block being extended minus time of the anchor's parent
 * @param[in] nHeightDiff        height of the block being extended minus height of the anchor (>= 0)
 * @param[in] powLimit           easiest allowed target (must have at least 32 leading zero bits)
 * @param[in] nHalfLife          half-life in seconds
 * @return                       next target, clamped to [1, powLimit]
 */
arith_uint256 CalculateASERT(const arith_uint256& refTarget, int64_t nPowTargetSpacing, int64_t nTimeDiff,
                             int64_t nHeightDiff, const arith_uint256& powLimit, int64_t nHalfLife) noexcept;

/** PQCH: compact target required for the block after pindexPrev under ASERT. */
unsigned int GetNextASERTWorkRequired(const CBlockIndex* pindexPrev, const Consensus::Params& params);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

#endif // BITCOIN_POW_H
