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
#include <support/cleanse.h>
#include <uint256.h>

#include <array>
#include <optional>
#include <string_view>
#include <cstdint>
#include <span>
#include <vector>

/**
 * PQCH: the standard P2MR wallet output (whitepaper, sections 21.3 and 21.5).
 *
 * Every address is a two-leaf P2MR tree:
 *   leaf A (daily use, hybrid):  <x-only key> OP_CHECKSIGVERIFY <ML-DSA-44 key> OP_CHECKSIGMLDSA
 *   leaf B (recovery):           <salt> OP_DROP <SLH-DSA-128s key> OP_CHECKSIGSLHDSA
 * both with leaf version 0xc2.
 *
 * Keys come from a 32-byte post-quantum seed, not from a BIP32 (elliptic curve) chain: no elliptic
 * curve public key from which the seed could be recovered with a quantum computer is ever stored or
 * shown, in the wallet file or on chain. The wallet keeps only the seed's id (a hash), the seed
 * itself (encrypted when the wallet is) and the public keys of each address.
 *
 *   node(path)       = HMAC-SHA512(seed, "PQCH/P2MR/v2/node" || path as 32-bit big endian)[0:32]
 *   Schnorr key      = HMAC-SHA512("PQCH/P2MR/v2/schnorr", node)[0:32]  (retried with a counter)
 *   ML-DSA-44 seed   = HMAC-SHA512("PQCH/P2MR/v2/ml-dsa-44", node)[0:32]
 *   salt             = HMAC-SHA512("PQCH/P2MR/v2/salt", node)[0:16]
 *   SLH-DSA-128s seed = HMAC-SHA512("PQCH/P2MR/v2/slh-dsa-128s", node(prefix / RECOVERY_KEY_INDEX'))[0:32]
 *
 * The Schnorr key is only one half of leaf A: recovering it from the x-only key that leaf A reveals
 * when spent gives nothing else. Generating an SLH-DSA-128s key takes ~80 ms, too slow for a
 * keypool of a thousand addresses, so one SLH-DSA key serves a whole descriptor (path prefix);
 * SLH-DSA is stateless, so reuse is safe. The per-address salt keeps leaf B's hash, revealed as the
 * sibling when leaf A is spent, different for every address.
 */
namespace p2mr {

inline constexpr size_t SEED_SIZE{32};
inline constexpr size_t SALT_SIZE{16};
inline constexpr uint32_t HARDENED{0x80000000};
/** Last path step of a descriptor's recovery (SLH-DSA) key (hardened); never an address. */
inline constexpr uint32_t RECOVERY_KEY_INDEX{0x7FFFFFFF};
/** Largest relative timelock of a vault's recovery leaf: BIP68 counts blocks in 16 bits. */
inline constexpr uint32_t MAX_VAULT_OLDER{0xFFFF};
/** Account step of the vault paths: addresses at 360h/coin/VAULT_ACCOUNT/{0h,1h}/(index)h of a vault seed. */
inline constexpr uint32_t VAULT_ACCOUNT{2};
/** Account step of an owner's vault recovery keys: 360h/coin/VAULT_KEY_ACCOUNT/kh of the owner's seed. */
inline constexpr uint32_t VAULT_KEY_ACCOUNT{3};

using Path = std::vector<uint32_t>; //!< every step hardened (HARDENED bit set)

/** A wallet's post-quantum seed. Wiped when it goes away. */
struct PQSeed {
    std::array<unsigned char, SEED_SIZE> bytes{};

    PQSeed() = default;
    explicit PQSeed(std::span<const unsigned char> b);
    PQSeed(const PQSeed&) = default;
    PQSeed& operator=(const PQSeed&) = default;
    ~PQSeed() { memory_cleanse(bytes.data(), bytes.size()); }

    /** Public id: SHA256 tagged "PQCH/P2MR/v2/seed-id" of the seed. */
    uint256 Id() const;
};

/** The seed of a new wallet from the random master key material it creates (key and chain code). */
PQSeed SeedFromMasterKey(const CExtKey& master);

/**
 * The seed from 24 BIP39 words and an optional passphrase (script/bip39.h):
 * HMAC-SHA512("PQCH/P2MR/v2/bip39", BIP39 seed)[0:32]. Nullopt if the words or passphrase are invalid.
 */
std::optional<PQSeed> SeedFromMnemonic(std::string_view mnemonic, std::string_view passphrase);

/**
 * The seed of a wallet's vault number `index` (contrib/pqch/COFRE.md):
 * HMAC-SHA512(wallet seed, "PQCH/P2MR/v2/vault" || index as 32-bit big endian)[0:32]. The recovery
 * words of the agent's wallet therefore restore its vaults; the owner gets a copy of this seed only,
 * never the agent's wallet seed.
 */
PQSeed VaultSeed(const PQSeed& wallet_seed, uint32_t index);
/** Path of an owner's vault recovery key number `index`: 360h/coin_typeh/3h/indexh. */
Path VaultKeyPath(bool test_chain, uint32_t index);

/** Public data of one address: what the descriptor cache keeps (no secrets). */
struct AddressKeys {
    XOnlyPubKey schnorr;
    std::vector<unsigned char> mldsa;  //!< ML-DSA-44 public key (1312 bytes)
    std::vector<unsigned char> slhdsa; //!< SLH-DSA-128s public key of the descriptor (32 bytes)
    std::array<unsigned char, SALT_SIZE> salt{};

    SERIALIZE_METHODS(AddressKeys, obj) { READWRITE(obj.schnorr, obj.mldsa, obj.slhdsa, obj.salt); }
    bool IsValid() const { return mldsa.size() == pqc::MLDSA44_PUBKEY_SIZE && slhdsa.size() == pqc::SLHDSA128S_PUBKEY_SIZE; }
    friend bool operator==(const AddressKeys&, const AddressKeys&) = default;
};

/** Private keys of leaf A. */
struct LeafAKeys {
    CKey schnorr;
    pqc::KeyPair mldsa;
    std::array<unsigned char, SALT_SIZE> salt{};
};

[[nodiscard]] bool DeriveLeafAKeys(const PQSeed& seed, const Path& path, LeafAKeys& out);
/** The SLH-DSA key at `recovery_path`. Slow (~80 ms). */
[[nodiscard]] bool DeriveRecoveryKey(const PQSeed& seed, const Path& recovery_path, pqc::KeyPair& out);
/** Public part of DeriveRecoveryKey, memoised in memory. */
[[nodiscard]] bool DeriveRecoveryPubKey(const PQSeed& seed, const Path& recovery_path, std::vector<unsigned char>& out);
/** Everything public about the address at `path`, whose recovery key is at `recovery_path`. */
[[nodiscard]] bool DeriveAddressKeys(const PQSeed& seed, const Path& path, const Path& recovery_path, AddressKeys& out);

CScript HybridLeaf(const XOnlyPubKey& schnorr, const std::vector<unsigned char>& mldsa);
/**
 * Leaf B. With older > 0 (a vault, see below) it starts with <older> OP_CHECKSEQUENCEVERIFY OP_DROP:
 * the recovery key can spend a coin only `older` blocks after the coin was confirmed.
 */
CScript RecoveryLeaf(const std::array<unsigned char, SALT_SIZE>& salt, const std::vector<unsigned char>& slhdsa, uint32_t older = 0);

/**
 * What a signer needs to spend a standard P2MR output or a vault output (kept in signing providers
 * by root).
 *
 * A vault (p2mr_vault() descriptor, contrib/pqch/COFRE.md) is the same two-leaf tree, but its
 * recovery key belongs to another seed (the owner's), and its leaf B may carry a relative timelock.
 */
struct SpendData {
    AddressKeys keys;
    uint256 seed_id;          //!< id of the seed the keys come from
    Path path;                //!< the address's path
    Path recovery_path;       //!< the recovery key's path
    uint256 recovery_seed_id; //!< vault: id of the recovery key's seed (null: seed_id)
    uint32_t older{0};        //!< vault: relative timelock of leaf B in blocks (0: none)

    const uint256& RecoverySeedId() const { return recovery_seed_id.IsNull() ? seed_id : recovery_seed_id; }

    CScript Leaf(bool recovery) const;
    uint256 LeafHash(bool recovery) const;
    uint256 Root() const;
    /** Control block for leaf A or B: leaf version | 1, then the other leaf's hash. */
    std::vector<unsigned char> Control(bool recovery) const;
    CScript ScriptPubKey() const;
};

/** Witness sizes of the two spending paths, element count included (+1 byte per signature with an explicit sighash type). */
int64_t HybridWitnessSize(bool explicit_hashtype);
int64_t RecoveryWitnessSize(bool explicit_hashtype, uint32_t older = 0);

} // namespace p2mr

#endif // BITCOIN_SCRIPT_P2MR_H
