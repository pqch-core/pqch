// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_TX_VERIFY_H
#define BITCOIN_CONSENSUS_TX_VERIFY_H

#include <consensus/amount.h>
#include <script/verify_flags.h>

#include <cstdint>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewCache;
class CScript;
class CTransaction;
class TxValidationState;

/** Transaction validation functions */

namespace Consensus {
/**
 * Check whether all inputs of this transaction are valid (no double spends and amounts)
 * This does not modify the UTXO set. This does not check scripts and sigs.
 * @param[out] txfee Set to the transaction fee if successful.
 * Preconditions: tx.IsCoinBase() is false.
 */
/**
 * PQCH rule 1 (whitepaper, section 21.3), enforced from the activation of the post-quantum rules
 * (genesis on the main network), coinbase included. The only valid outputs are:
 *  - P2MR (witness v1, 32-byte Merkle root): coins protected by post-quantum signatures;
 *  - OP_RETURN (provably unspendable data);
 *  - pay-to-anchor (witness v1, 0x4e73): keyless anchors for fee bumping, e.g. in Lightning;
 *  - witness versions 2 to 16, reserved for future soft forks.
 * Outputs without witness (P2PK, P2PKH, P2SH, bare scripts) and witness v0 are invalid, so no coin
 * is ever protected by an elliptic-curve key or a 160-bit hash alone.
 */
[[nodiscard]] bool IsPQAllowedOutput(const CScript& script_pub_key);
[[nodiscard]] bool CheckPQOutputs(const CTransaction& tx, TxValidationState& state);

[[nodiscard]] bool CheckTxInputs(const CTransaction& tx, TxValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee,
                                 int coinbase_maturity);
} // namespace Consensus

/** Auxiliary functions for transaction validation (ideally should not be exposed) */

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/**
 * Compute total signature operation cost of a transaction.
 * @param[in] tx     Transaction for which we are computing the cost
 * @param[in] inputs Map of previous transactions that have outputs we're spending
 * @param[in] flags Script verification flags
 * @return Total signature operation cost of tx
 */
int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, script_verify_flags flags);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * For each input that is not sequence locked, the corresponding entries in
 * prevHeights are set to 0 as they do not affect the calculation.
 */
std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>& prevHeights, const CBlockIndex& block);

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair);
/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
 * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>& prevHeights, const CBlockIndex& block);

#endif // BITCOIN_CONSENSUS_TX_VERIFY_H
