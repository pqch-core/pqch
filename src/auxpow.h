// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AUXPOW_H
#define BITCOIN_AUXPOW_H

#include <serialize.h>
#include <uint256.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/**
 * PQCH: merged mining with Monero (whitepaper, section 13).
 *
 * A Monero miner (through P2Pool's merge mining API) puts in the tx_extra of its coinbase a merge
 * mining tag (0x03) holding the root of a small Merkle tree of auxiliary chain block hashes. If the
 * RandomX hash of that Monero block meets the PQCH target, the same work proves the PQCH block.
 * The Monero block does not need to be valid or accepted on the Monero network.
 *
 * A PQCH header with bit VERSION_AUXPOW (0x100) set in nVersion is followed, on the wire and on
 * disk, by a CAuxPow. The block id stays SHA256d of the 80-byte header and does not commit to the
 * CAuxPow; see CheckAuxPowHeader for what binds the work to the block.
 *
 * Compact proof. P2Pool coinbases pay hundreds of miners and can be tens of KB, which is too much
 * for every header. The header therefore carries the Keccak sponge state after the first
 * `absorbed_len` bytes of the coinbase transaction prefix, plus the remaining bytes. Because the
 * split point cannot be checked from the header alone, the block carries those first bytes too
 * (CAuxPowCoinbaseHead) and the full prefix is parsed as a canonical Monero coinbase when the block
 * is validated (CheckAuxPowCoinbase). This is what stops one Monero proof of work from standing
 * behind two different PQCH blocks (the problem Tari fixed in GHSA-3qmx-q9pv-f3m4).
 *
 * Hashing follows Monero and P2Pool exactly (Keccak-256 with the original padding, Monero's
 * transaction tree hash, P2Pool's aux slot), checked against P2Pool v4 merkle.cpp and pool_block.h.
 */
namespace auxpow {

/** nVersion bit that marks a header followed by a CAuxPow. */
inline constexpr int32_t VERSION_AUXPOW{0x100};

/**
 * Monero hard fork 17 (FCMP++ and Carrot; RandomX v2 from the same version, see pow_randomx.h).
 * From this Monero major version on, the transaction tree has two more leaves right after the
 * coinbase (the FCMP++ tree layer count, as a 32-byte value with the count in its first byte, and
 * the FCMP++ tree root), the block blob ends with those two values, and coinbase outputs are Carrot
 * outputs. The hashing blob still counts only real transactions (coinbase included). Same layout as
 * P2Pool v5 (block_template.cpp, merge_mining_client.cpp).
 */
inline constexpr uint64_t MONERO_HF_FCMP_PP{17};
/** Extra leaves in the Monero transaction tree from MONERO_HF_FCMP_PP on. */
inline constexpr uint64_t FCMP_PP_TREE_LEAVES{2};
/** Carrot coinbase output (txout_to_carrot_v1): one-time address, 3-byte view tag, 16-byte encrypted Janus anchor. */
inline constexpr unsigned char TXOUT_TO_CARROT_V1{0x01};
inline constexpr size_t CARROT_OUTPUT_SIZE{32 + 3 + 16};

/** Monero major version: the first varint of a Monero block header. Returns false if unreadable. */
bool MoneroMajorVersion(std::span<const unsigned char> parent_header, uint64_t& major_version);

/** Size limits (consensus). */
inline constexpr size_t MAX_PARENT_HEADER_SIZE{96};     //!< Monero header: 2 varints, timestamp varint, 32-byte prev id, 4-byte nonce
inline constexpr uint64_t MAX_PARENT_TX_COUNT{1 << 20};  //!< transactions in the Monero block, coinbase included
inline constexpr size_t MAX_COINBASE_BRANCH{21};         //!< log2(MAX_PARENT_TX_COUNT) + 1
inline constexpr size_t KECCAK_RATE{136};                //!< Keccak-256 block size in bytes
inline constexpr uint64_t MAX_COINBASE_HEAD_SIZE{256 * 1024}; //!< coinbase prefix bytes summarised by the midstate
inline constexpr size_t MAX_COINBASE_EXTRA_SIZE{512};    //!< below Monero's 1060; real P2Pool proofs 75-83 bytes (calibrated 23 Nov 2026)
inline constexpr size_t MAX_AUX_BRANCH{8};               //!< up to 256 merge mined chains
inline constexpr uint64_t MAX_COINBASE_OUTPUTS{16384};
/**
 * Largest serialized CAuxPow within the limits above. With it, a full headers message
 * (2000 merged mined headers) still fits in one P2P message (static_assert in net_processing.cpp).
 */
inline constexpr size_t MAX_AUXPOW_SIZE{(1 + MAX_PARENT_HEADER_SIZE) + 32 + 3 + (1 + MAX_COINBASE_BRANCH * 32) +
                                        25 * 8 + 3 + (1 + KECCAK_RATE - 1) + (3 + MAX_COINBASE_EXTRA_SIZE) +
                                        (1 + MAX_AUX_BRANCH * 32)};

/** Keccak-256 as used by Monero (cn_fast_hash): rate 136, padding 0x01 ... 0x80. */
class CKeccak256
{
public:
    using State = std::array<uint64_t, 25>;
    CKeccak256() = default;
    /** Resume from a state taken after absorbing `absorbed` bytes (a multiple of KECCAK_RATE). */
    CKeccak256(const State& state, uint64_t absorbed);
    CKeccak256& Write(std::span<const unsigned char> data);
    void Finalize(std::span<unsigned char, 32> out);
    /** Current state; only meaningful when the bytes written so far are a multiple of KECCAK_RATE. */
    const State& GetState() const { return m_state; }
    uint64_t Absorbed() const { return m_absorbed; }
    bool AtBlockBoundary() const { return m_buffer_len == 0; }

private:
    State m_state{};
    std::array<unsigned char, KECCAK_RATE> m_buffer{};
    size_t m_buffer_len{0};
    uint64_t m_absorbed{0}; //!< bytes already permuted into the state
};

uint256 Keccak256(std::span<const unsigned char> data);
/** Keccak-256 of two concatenated 32-byte values (a tree node). */
uint256 KeccakPair(const uint256& a, const uint256& b);

/** Monero's tree hash (tree_hash in crypto/tree-hash.c, merkle_hash in P2Pool). Empty input: null. */
uint256 MoneroTreeHash(const std::vector<uint256>& hashes);

/**
 * Root of a Monero tree from a leaf, its index and the tree size (P2Pool's get_root_from_proof).
 * Returns false if the proof does not have exactly the length the tree size implies: the depth is
 * bound to the size, so a proof cannot place the leaf anywhere else.
 */
bool MoneroRootFromProof(const uint256& leaf, const std::vector<uint256>& proof, uint64_t index, uint64_t count, uint256& root);

/** Proof for leaf `index` (used by the node to build AuxPoW from a full Monero block, and by tests). */
std::vector<uint256> MoneroProof(const std::vector<uint256>& hashes, uint64_t index);

/** P2Pool's slot for a chain: SHA256(chain_id | nonce LE | 'm'), first 4 bytes LE, mod n. */
uint32_t AuxSlot(const uint256& chain_id, uint32_t nonce, uint32_t n_aux_chains);

/** P2Pool's merge mining parameters (pool_block.h encode/decode_merkle_tree_data). */
uint64_t EncodeMergeMiningParams(uint32_t n_aux_chains, uint32_t nonce);
/** Strict decoding: only the canonical encoding of (n_aux_chains, nonce) is accepted. */
bool DecodeMergeMiningParams(uint64_t value, uint32_t& n_aux_chains, uint32_t& nonce);

/** Monero varint (7 bits per byte, little endian); canonical encodings only. */
void WriteMoneroVarInt(std::vector<unsigned char>& out, uint64_t v);
bool ReadMoneroVarInt(std::span<const unsigned char>& in, uint64_t& v);

struct MergeMiningTag {
    uint32_t n_aux_chains{0};
    uint32_t nonce{0};
    uint256 aux_root;
};

/**
 * Parse a Monero tx_extra strictly: only the subfields Monero coinbases use (0x00 padding, 0x01
 * public key, 0x02 nonce, 0x03 merge mining, 0x04 additional public keys) and exactly one merge
 * mining tag. Anything else fails.
 */
bool ParseCoinbaseExtra(std::span<const unsigned char> extra, MergeMiningTag& tag, std::string& error);

/** Header part of the merged mining proof. */
struct CAuxPow {
    std::vector<unsigned char> parent_header; //!< Monero block header bytes, as hashed
    uint256 seed_hash;                        //!< RandomX key of the Monero block
    uint64_t tx_count{1};                     //!< transactions in the Monero block (coinbase included)
    std::vector<uint256> coinbase_branch;     //!< proof of the coinbase (index 0) in the Monero transaction tree
    CKeccak256::State coinbase_midstate{};    //!< Keccak state after the first `head_size` bytes of the coinbase prefix
    uint64_t head_size{0};                    //!< multiple of KECCAK_RATE
    std::vector<unsigned char> coinbase_tail; //!< rest of the coinbase prefix before tx_extra (< KECCAK_RATE bytes)
    std::vector<unsigned char> coinbase_extra; //!< the coinbase tx_extra
    std::vector<uint256> aux_branch;          //!< proof of the PQCH block hash in the merge mining tree

    SERIALIZE_METHODS(CAuxPow, obj)
    {
        READWRITE(obj.parent_header, obj.seed_hash, VARINT(obj.tx_count), obj.coinbase_branch);
        for (auto& word : obj.coinbase_midstate) READWRITE(word);
        READWRITE(VARINT(obj.head_size), obj.coinbase_tail, obj.coinbase_extra, obj.aux_branch);
    }

    /** Monero coinbase transaction hash (v2, RingCT type null), from the midstate. */
    uint256 CoinbaseHash() const;
    /** Leaves in the Monero transaction tree: tx_count, plus the two FCMP++ leaves from hard fork 17. 0 if the header is unreadable. */
    uint64_t TreeLeafCount() const;
    /** Monero block hashing blob: header || tree root || varint(tx count). Empty if the branch is invalid. */
    std::vector<unsigned char> HashingBlob() const;
};

/**
 * Header-level checks, everything except the RandomX hash (the caller does that with `seed_hash`
 * and `blob`, after the cheaper checks). Binds the work to `block_hash` (the PQCH block id) through
 * the merge mining tag at PQCH's slot (`chain_id` = genesis hash).
 */
bool CheckAuxPowHeader(const CAuxPow& auxpow, const uint256& block_hash, const uint256& chain_id,
                       std::vector<unsigned char>& blob, std::string& error);

/** Block part: the coinbase prefix bytes that the header's midstate summarises. */
struct CAuxPowCoinbaseHead {
    std::vector<unsigned char> head;
    SERIALIZE_METHODS(CAuxPowCoinbaseHead, obj) { READWRITE(obj.head); }
};

/**
 * Block-level check: `head` hashes to the header's midstate and head || tail is a canonical
 * Monero v2 coinbase prefix (version 2, unlock time, one gen input, outputs to keys; Carrot outputs
 * from Monero hard fork 17 on), ending right
 * where tx_extra starts. Without it the split between midstate and tail would be free.
 */
bool CheckAuxPowCoinbase(const CAuxPow& auxpow, const CAuxPowCoinbaseHead& head, std::string& error);

/** Build a CAuxPow and its coinbase head from a full Monero block blob (P2Pool's submission). */
bool BuildFromMoneroBlock(std::span<const unsigned char> monero_block, const uint256& seed_hash,
                          const std::vector<uint256>& aux_branch, CAuxPow& auxpow, CAuxPowCoinbaseHead& head,
                          std::string& error);

} // namespace auxpow

#endif // BITCOIN_AUXPOW_H
