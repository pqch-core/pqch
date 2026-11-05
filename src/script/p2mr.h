// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_P2MR_H
#define BITCOIN_SCRIPT_P2MR_H

#include <crypto/pqc/pqc.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <array>
#include <cstdint>
#include <vector>

/**
 * PQCH: the standard P2MR wallet output (whitepaper, sections 21.3 and 21.5).
 *
 * Every address is a two-leaf P2MR tree:
 *   leaf A (daily use, hybrid):  <x-only key> OP_CHECKSIGVERIFY <ML-DSA-44 key> OP_CHECKSIGMLDSA
 *   leaf B (recovery):           <salt> OP_DROP <SLH-DSA-128s key> OP_CHECKSIGSLHDSA
 * both with leaf version 0xc2.
 *
 * Keys. Each address has a BIP32 child key `k` (hardened derivation only). Its own public key is
 * never used on chain: the Schnorr key, the ML-DSA seed and the salt are separate HMAC-SHA512
 * outputs of `k`. A quantum attacker who recovers the Schnorr private key from the x-only key
 * revealed when leaf A is spent learns nothing about `k`, hence nothing about the ML-DSA key.
 *
 * Recovery key. Generating an SLH-DSA-128s key takes ~80 ms, too slow for a keypool of a thousand
 * addresses, so one SLH-DSA key serves a whole descriptor (it is derived from the child key at
 * RECOVERY_KEY_INDEX). SLH-DSA is stateless, so reuse is safe. The per-address salt keeps leaf B's
 * hash, which is revealed as the sibling when leaf A is spent, different for every address.
 */
namespace p2mr {

inline constexpr size_t SALT_SIZE{16};
/** Derivation position of a descriptor's recovery (SLH-DSA) key; never used for an address. */
inline constexpr uint32_t RECOVERY_KEY_INDEX{0x7FFFFFFF};

/** Public data of one wallet address: what the descriptor cache keeps (no private keys needed). */
struct AddressKeys {
    XOnlyPubKey schnorr;
    std::vector<unsigned char> mldsa;  //!< ML-DSA-44 public key (1312 bytes)
    std::vector<unsigned char> slhdsa; //!< SLH-DSA-128s public key of the descriptor (32 bytes)
    std::array<unsigned char, SALT_SIZE> salt{};
    CPubKey recovery_owner; //!< BIP32 child key at RECOVERY_KEY_INDEX (its private key derives the SLH-DSA key)

    SERIALIZE_METHODS(AddressKeys, obj) { READWRITE(obj.schnorr, obj.mldsa, obj.slhdsa, obj.salt, obj.recovery_owner); }
    bool IsValid() const { return mldsa.size() == pqc::MLDSA44_PUBKEY_SIZE && slhdsa.size() == pqc::SLHDSA128S_PUBKEY_SIZE; }
    friend bool operator==(const AddressKeys&, const AddressKeys&) = default;
};

/** Private keys of leaf A, derived from the address's BIP32 child key. */
struct LeafAKeys {
    CKey schnorr;
    pqc::KeyPair mldsa;
    std::array<unsigned char, SALT_SIZE> salt{};
};

/** Leaf A keys from the BIP32 child key of an address. */
[[nodiscard]] bool DeriveLeafAKeys(const CKey& child, LeafAKeys& out);
/** The descriptor's SLH-DSA key from the child key at RECOVERY_KEY_INDEX. Slow (~80 ms). */
[[nodiscard]] bool DeriveRecoveryKey(const CKey& recovery_child, pqc::KeyPair& out);
/** Public part of DeriveRecoveryKey, memoised in memory (keyed by the recovery child's public key). */
[[nodiscard]] bool DeriveRecoveryPubKey(const CKey& recovery_child, std::vector<unsigned char>& out);
/** Everything public about an address. */
[[nodiscard]] bool DeriveAddressKeys(const CKey& child, const CKey& recovery_child, AddressKeys& out);

CScript HybridLeaf(const XOnlyPubKey& schnorr, const std::vector<unsigned char>& mldsa);
CScript RecoveryLeaf(const std::array<unsigned char, SALT_SIZE>& salt, const std::vector<unsigned char>& slhdsa);

/** What a signer needs to spend a standard P2MR output (kept in signing providers by root). */
struct SpendData {
    AddressKeys keys;
    CPubKey owner; //!< BIP32 child key of the address (its private key derives leaf A)

    CScript Leaf(bool recovery) const;
    uint256 LeafHash(bool recovery) const;
    uint256 Root() const;
    /** Control block for leaf A or B: leaf version | 1, then the other leaf's hash. */
    std::vector<unsigned char> Control(bool recovery) const;
    CScript ScriptPubKey() const;
};

SpendData MakeSpendData(const AddressKeys& keys, const CPubKey& owner);

/** Witness sizes of the two spending paths (signatures with SIGHASH_DEFAULT, +1 byte each otherwise). */
int64_t HybridWitnessSize(bool explicit_hashtype);
int64_t RecoveryWitnessSize(bool explicit_hashtype);

} // namespace p2mr

#endif // BITCOIN_SCRIPT_P2MR_H
