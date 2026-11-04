// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_PQCH_H
#define BITCOIN_TEST_UTIL_PQCH_H

#include <crypto/pqc/pqc.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * PQCH test helpers: P2MR outputs with post-quantum leaves, and signing them.
 * The same constructions are what a wallet does (whitepaper, section 21.3).
 */
namespace pqch_test {

/** Daily-use leaf A: <ML-DSA-44 key> OP_CHECKSIGMLDSA. */
CScript LeafMLDSA(const std::vector<unsigned char>& mldsa_pubkey);
/** Hybrid leaf A (wallet default): <x-only key> OP_CHECKSIGVERIFY <ML-DSA-44 key> OP_CHECKSIGMLDSA. */
CScript LeafHybrid(const std::vector<unsigned char>& xonly_pubkey, const std::vector<unsigned char>& mldsa_pubkey);
/** Recovery leaf B: <SLH-DSA-128s key> OP_CHECKSIGSLHDSA. */
CScript LeafSLHDSA(const std::vector<unsigned char>& slhdsa_pubkey);

/** A P2MR script tree with one or two leaves, all with leaf version 0xc2 unless given. */
struct P2MRTree {
    std::vector<CScript> leaves;
    std::vector<uint8_t> versions;

    uint256 LeafHash(size_t i) const;
    uint256 Root() const;
    /** Control block for leaf i: leaf version | 1, then the Merkle path. */
    std::vector<unsigned char> Control(size_t i) const;
    /** scriptPubKey: OP_1 <32-byte root>. */
    CScript ScriptPubKey() const;
};

P2MRTree MakeTree(std::vector<CScript> leaves, uint8_t leaf_version = P2MR_LEAF_PQSCRIPT);

/** BIP341/342 signature hash for spending input `in` through leaf `leaf_hash`. */
uint256 LeafSigHash(const CMutableTransaction& tx, unsigned int in, const std::vector<CTxOut>& spent_outputs,
                    const uint256& leaf_hash, uint8_t hash_type, uint8_t key_version);

/** Post-quantum signature of `sighash` (deterministic), with the hash type byte appended unless SIGHASH_DEFAULT. */
std::vector<unsigned char> SignMLDSA(const std::vector<unsigned char>& seckey, const uint256& sighash, uint8_t hash_type = 0);
std::vector<unsigned char> SignSLHDSA(const std::vector<unsigned char>& seckey, const uint256& sighash, uint8_t hash_type = 0);

/**
 * A Bitcoin bech32/bech32m address from a test vector, re-encoded with the selected chain's
 * prefix (pqc, pqcrt): same witness version and program, PQCH human-readable part.
 */
std::string ToChainBech32Address(const std::string& bitcoin_address);

/** Deterministic key pairs for tests, from a one-byte tag. */
pqc::KeyPair MLDSAKey(uint8_t tag);
pqc::KeyPair SLHDSAKey(uint8_t tag);

} // namespace pqch_test

#endif // BITCOIN_TEST_UTIL_PQCH_H
