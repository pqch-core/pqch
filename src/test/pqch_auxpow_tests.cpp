// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>
#include <banman.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <hash.h>
#include <interfaces/mining.h>
#include <key_io.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <protocol.h>
#include <node/mergemining.h>
#include <pubkey.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <pow_randomx.h>
#include <primitives/block.h>
#include <rpc/server.h>
#include <streams.h>
#include <univalue.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <deque>
#include <memory>

using namespace auxpow;

namespace {

std::vector<unsigned char> Bytes(std::string_view hex) { return ParseHex(hex); }

uint256 FromBytes(std::string_view hex)
{
    const auto b{ParseHex(hex)};
    assert(b.size() == 32);
    uint256 r;
    std::copy(b.begin(), b.end(), r.begin());
    return r;
}

uint256 Tagged(uint8_t tag)
{
    uint256 r;
    for (size_t i = 0; i < 32; ++i) r.begin()[i] = static_cast<unsigned char>(tag + 7 * i);
    return r;
}

/** A synthetic Monero block (template) as P2Pool would submit it. */
struct MoneroBlock {
    uint64_t major{16};             //!< 17 and later: FCMP++ tree values and Carrot outputs (hard fork 17)
    uint32_t nonce{0};
    uint64_t height{3'000'000};
    size_t outputs{1};
    std::vector<unsigned char> extra;
    std::vector<uint256> tx_hashes; //!< other transactions (not the coinbase)
    unsigned char fcmp_pp_layers{7};
    uint256 fcmp_pp_root{Tagged(0x77)};
    bool carrot_outputs{false};     //!< false: by the version (Carrot from 17); true: force Carrot outputs

    std::vector<unsigned char> Header() const
    {
        std::vector<unsigned char> b;
        WriteMoneroVarInt(b, major);         // major version
        WriteMoneroVarInt(b, major);         // minor version
        WriteMoneroVarInt(b, 1'790'000'000); // timestamp
        const uint256 prev{Tagged(0x50)};
        b.insert(b.end(), prev.begin(), prev.end());
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(nonce >> (8 * i)));
        return b;
    }

    /** Miner transaction prefix: everything before the RingCT type byte (tx_extra included). */
    std::vector<unsigned char> MinerTxPrefix() const
    {
        std::vector<unsigned char> b;
        WriteMoneroVarInt(b, 2);          // version
        WriteMoneroVarInt(b, height + 60); // unlock time
        WriteMoneroVarInt(b, 1);          // one input
        b.push_back(0xff);                // txin_gen
        WriteMoneroVarInt(b, height);
        WriteMoneroVarInt(b, outputs);
        const bool carrot{carrot_outputs || major >= MONERO_HF_FCMP_PP};
        for (size_t i = 0; i < outputs; ++i) {
            WriteMoneroVarInt(b, 600'000'000'000ULL / outputs + i);
            const uint256 key{Tagged(static_cast<uint8_t>(i))};
            if (carrot) {
                b.push_back(TXOUT_TO_CARROT_V1); // one-time address, 3-byte view tag, 16-byte encrypted anchor
                b.insert(b.end(), key.begin(), key.end());
                for (int j = 0; j < 3 + 16; ++j) b.push_back(static_cast<unsigned char>(i + j));
            } else {
                b.push_back(0x03); // txout_to_tagged_key: key and view tag
                b.insert(b.end(), key.begin(), key.end());
                b.push_back(static_cast<unsigned char>(i));
            }
        }
        WriteMoneroVarInt(b, extra.size());
        b.insert(b.end(), extra.begin(), extra.end());
        return b;
    }

    std::vector<unsigned char> Serialize() const
    {
        std::vector<unsigned char> b{Header()};
        const auto tx{MinerTxPrefix()};
        b.insert(b.end(), tx.begin(), tx.end());
        b.push_back(0x00); // RingCT type null
        WriteMoneroVarInt(b, tx_hashes.size());
        for (const auto& h : tx_hashes) b.insert(b.end(), h.begin(), h.end());
        if (major >= MONERO_HF_FCMP_PP) {
            b.push_back(fcmp_pp_layers);
            b.insert(b.end(), fcmp_pp_root.begin(), fcmp_pp_root.end());
        }
        return b;
    }
};

/** P2Pool's tx_extra: public key, merge mining tag, extra nonce. */
std::vector<unsigned char> MakeExtra(uint32_t n_chains, uint32_t mm_nonce, const uint256& root, bool second_tag = false)
{
    std::vector<unsigned char> e{0x01};
    const uint256 pub{Tagged(0x11)};
    e.insert(e.end(), pub.begin(), pub.end());
    std::vector<unsigned char> body;
    WriteMoneroVarInt(body, EncodeMergeMiningParams(n_chains, mm_nonce));
    body.insert(body.end(), root.begin(), root.end());
    for (int i = 0; i < (second_tag ? 2 : 1); ++i) {
        e.push_back(0x03);
        WriteMoneroVarInt(e, body.size());
        e.insert(e.end(), body.begin(), body.end());
    }
    e.insert(e.end(), {0x02, 0x04, 0xde, 0xad, 0xbe, 0xef});
    return e;
}

/** Merge mining tree with the PQCH hash at its slot and filler hashes elsewhere. */
struct AuxTree {
    std::vector<uint256> leaves;
    uint32_t slot;
    AuxTree(const uint256& chain_id, const uint256& pqch_hash, uint32_t n, uint32_t mm_nonce)
    {
        slot = AuxSlot(chain_id, mm_nonce, n);
        for (uint32_t i = 0; i < n; ++i) leaves.push_back(i == slot ? pqch_hash : Tagged(0x80 + i));
    }
    uint256 Root() const { return MoneroTreeHash(leaves); }
    std::vector<uint256> Branch() const { return MoneroProof(leaves, slot); }
};

std::vector<unsigned char> Serialized(const CAuxPow& a)
{
    DataStream s;
    s << a;
    const auto b{MakeUCharSpan(s)};
    return {b.begin(), b.end()};
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_auxpow_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(keccak256_monero)
{
    // Original Keccak padding (Monero's cn_fast_hash), not SHA3-256.
    BOOST_CHECK_EQUAL(HexStr(Keccak256({})), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    const std::string abc{"abc"};
    BOOST_CHECK_EQUAL(HexStr(Keccak256(MakeUCharSpan(abc))), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    // Streaming in odd pieces and resuming from a midstate give the one-shot hash.
    std::vector<unsigned char> data(1000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<unsigned char>(i * 31 + 7);
    const uint256 one_shot{Keccak256(data)};
    CKeccak256 k;
    for (size_t i = 0; i < data.size(); i += 37) k.Write(std::span{data}.subspan(i, std::min<size_t>(37, data.size() - i)));
    uint256 streamed;
    k.Finalize(std::span<unsigned char, 32>{streamed.begin(), 32});
    BOOST_CHECK(streamed == one_shot);

    CKeccak256 head;
    head.Write(std::span{data}.first(3 * KECCAK_RATE));
    BOOST_REQUIRE(head.AtBlockBoundary());
    CKeccak256 resumed{head.GetState(), 3 * KECCAK_RATE};
    uint256 out;
    resumed.Write(std::span{data}.subspan(3 * KECCAK_RATE)).Finalize(std::span<unsigned char, 32>{out.begin(), 32});
    BOOST_CHECK(out == one_shot);
}

BOOST_AUTO_TEST_CASE(monero_tree_and_proofs)
{
    BOOST_CHECK(MoneroTreeHash({}).IsNull());
    BOOST_CHECK(MoneroTreeHash({Tagged(1)}) == Tagged(1));
    BOOST_CHECK(MoneroTreeHash({Tagged(1), Tagged(2)}) == KeccakPair(Tagged(1), Tagged(2)));
    // Three leaves (Monero tree-hash.c): the first leaf is carried up, the last two are paired.
    BOOST_CHECK(MoneroTreeHash({Tagged(1), Tagged(2), Tagged(3)}) == KeccakPair(Tagged(1), KeccakPair(Tagged(2), Tagged(3))));

    for (uint64_t n = 1; n <= 40; ++n) {
        std::vector<uint256> leaves;
        for (uint64_t i = 0; i < n; ++i) leaves.push_back(Tagged(static_cast<uint8_t>(i)));
        const uint256 root{MoneroTreeHash(leaves)};
        for (uint64_t i = 0; i < n; ++i) {
            const auto proof{MoneroProof(leaves, i)};
            uint256 r;
            BOOST_REQUIRE(MoneroRootFromProof(leaves[i], proof, i, n, r));
            BOOST_CHECK(r == root);
            // The depth is bound to the tree size: a longer or shorter proof is refused.
            auto longer{proof};
            longer.push_back(Tagged(0xee));
            BOOST_CHECK(!MoneroRootFromProof(leaves[i], longer, i, n, r));
            if (!proof.empty()) {
                auto shorter{proof};
                shorter.pop_back();
                BOOST_CHECK(!MoneroRootFromProof(leaves[i], shorter, i, n, r));
            }
            // A proof for one index does not place the leaf at another one.
            if (n > 1) {
                const uint64_t j{(i + 1) % n};
                if (MoneroRootFromProof(leaves[i], proof, j, n, r)) BOOST_CHECK(r != root);
            }
        }
        uint256 r;
        BOOST_CHECK(!MoneroRootFromProof(leaves[0], MoneroProof(leaves, 0), n, n, r)); // index out of range
    }
    // Huge tree sizes are refused at once (found by fuzzing: the width loop never ended above 2^63).
    uint256 r;
    BOOST_CHECK(!MoneroRootFromProof(Tagged(1), {}, 0, std::numeric_limits<uint64_t>::max(), r));
    BOOST_CHECK(!MoneroRootFromProof(Tagged(1), {}, 5, (uint64_t{1} << 63) + 1, r));
}

BOOST_AUTO_TEST_CASE(merge_mining_params_and_slot)
{
    for (uint32_t n : {1U, 2U, 3U, 4U, 5U, 8U, 9U, 100U, 255U, 256U}) {
        for (uint32_t nonce : {0U, 1U, 0x1234U, 0xffffffffU}) {
            const uint64_t v{EncodeMergeMiningParams(n, nonce)};
            uint32_t n2, nonce2;
            BOOST_REQUIRE(DecodeMergeMiningParams(v, n2, nonce2));
            BOOST_CHECK_EQUAL(n2, n);
            BOOST_CHECK_EQUAL(nonce2, nonce);
            BOOST_CHECK(AuxSlot(Tagged(9), nonce, n) < n);
        }
    }
    // P2Pool's encoding: n_bits - 1 in the low 3 bits, then n - 1 in n_bits bits, then the nonce.
    BOOST_CHECK_EQUAL(EncodeMergeMiningParams(1, 0), 0U);
    BOOST_CHECK_EQUAL(EncodeMergeMiningParams(2, 5), (0U | (1U << 3) | (5U << 4)));
    BOOST_CHECK_EQUAL(EncodeMergeMiningParams(3, 1), (1U | (2U << 3) | (1U << 5)));
    uint32_t n, nonce;
    // A wider-than-needed bit field is not canonical.
    BOOST_CHECK(!DecodeMergeMiningParams(1 | (0 << 3), n, nonce));
    // A nonce beyond 32 bits is refused.
    BOOST_CHECK(!DecodeMergeMiningParams(uint64_t{1} << 40, n, nonce));
    // Slot of a single chain is always 0; known P2Pool value: SHA256(chain_id | nonce | 'm').
    BOOST_CHECK_EQUAL(AuxSlot(Tagged(1), 77, 1), 0U);
}

BOOST_AUTO_TEST_CASE(monero_varint)
{
    for (uint64_t v : {uint64_t{0}, uint64_t{1}, uint64_t{127}, uint64_t{128}, uint64_t{300}, uint64_t{1} << 35, ~uint64_t{0}}) {
        std::vector<unsigned char> b;
        WriteMoneroVarInt(b, v);
        std::span<const unsigned char> in{b};
        uint64_t r;
        BOOST_REQUIRE(ReadMoneroVarInt(in, r));
        BOOST_CHECK_EQUAL(r, v);
        BOOST_CHECK(in.empty());
    }
    // Non-canonical (trailing zero group), truncated and overlong encodings fail.
    for (const auto& hex : {"8000", "ff", "80808080808080808080", "ffffffffffffffffff7f"}) {
        const auto b{Bytes(hex)};
        std::span<const unsigned char> in{b};
        uint64_t r;
        BOOST_CHECK_MESSAGE(!ReadMoneroVarInt(in, r), hex);
    }
}

BOOST_AUTO_TEST_CASE(real_monero_hashing_blob)
{
    // A Monero mainnet block template and its hashing blob (from Tari's monero_rx tests).
    const auto block{Bytes("0c0c94debaf805beb3489c722a285c092a32e7c6893abfc7d069699c8326fc3445a749c5276b6200000000029b892201ffdf882201b699d4c8b1ec020223df524af2a2ef5f870adb6e1ceb03a475c39f8b9ef76aa50b46ddd2a18349402b012839bfa19b7524ec7488917714c216ca254b38ed0424ca65ae828a7c006aeaf10208f5316a7f6b99cca60000")};
    const std::string expected_blob{"0c0c94debaf805beb3489c722a285c092a32e7c6893abfc7d069699c8326fc3445a749c5276b6200000000602d0d4710e2c2d38da0cce097accdf5dc18b1d34323880c1aae90ab8f6be6e201"};
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    std::string error;
    BOOST_REQUIRE_MESSAGE(BuildFromMoneroBlock(block, FromBytes("9f02e032f9b15d2aded991e0f68cc3c3427270b568b782e55fbd269ead0bad97"), {}, a, head, error), error);
    BOOST_CHECK_EQUAL(HexStr(a.HashingBlob()), expected_blob);
    BOOST_CHECK(CheckAuxPowCoinbase(a, head, error));
    // It has no merge mining tag, so it cannot prove a PQCH block.
    std::vector<unsigned char> blob;
    BOOST_CHECK(!CheckAuxPowHeader(a, Tagged(1), Tagged(2), blob, error));
    BOOST_CHECK_EQUAL(error, "no merge mining tag in the Monero coinbase");
}

BOOST_AUTO_TEST_CASE(auxpow_header_binding)
{
    const uint256 chain_id{Tagged(0xc1)};
    const uint256 pqch_hash{Tagged(0xb1)};
    for (uint32_t n_chains : {1U, 2U, 3U, 7U}) {
        const uint32_t mm_nonce{12345};
        AuxTree tree{chain_id, pqch_hash, n_chains, mm_nonce};
        MoneroBlock mb;
        mb.extra = MakeExtra(n_chains, mm_nonce, tree.Root());
        mb.tx_hashes = {Tagged(0x31), Tagged(0x32), Tagged(0x33)};
        CAuxPow a;
        CAuxPowCoinbaseHead head;
        std::string error;
        BOOST_REQUIRE_MESSAGE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), tree.Branch(), a, head, error), error);
        std::vector<unsigned char> blob;
        BOOST_CHECK_MESSAGE(CheckAuxPowHeader(a, pqch_hash, chain_id, blob, error), error);
        BOOST_CHECK(blob == a.HashingBlob());

        // Another block id, another chain id (other slot) or a tampered branch: refused.
        BOOST_CHECK(!CheckAuxPowHeader(a, Tagged(0xb2), chain_id, blob, error));
        if (n_chains > 1) {
            BOOST_CHECK(!CheckAuxPowHeader(a, pqch_hash, Tagged(0xc2), blob, error) || AuxSlot(Tagged(0xc2), mm_nonce, n_chains) == tree.slot);
            CAuxPow bad{a};
            bad.aux_branch[0] = Tagged(0xff);
            BOOST_CHECK(!CheckAuxPowHeader(bad, pqch_hash, chain_id, blob, error));
        }
        // The coinbase branch must match the Monero transaction count.
        CAuxPow bad{a};
        bad.tx_count = 9;
        BOOST_CHECK(!CheckAuxPowHeader(bad, pqch_hash, chain_id, blob, error));

        // The proof survives serialization.
        DataStream s;
        s << a;
        CAuxPow b;
        s >> b;
        BOOST_CHECK(Serialized(a) == Serialized(b));
    }

    // Two merge mining tags: refused (which one would count is ambiguous).
    AuxTree tree{chain_id, pqch_hash, 1, 0};
    MoneroBlock mb;
    mb.extra = MakeExtra(1, 0, tree.Root(), /*second_tag=*/true);
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    std::string error;
    BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), {}, a, head, error));
    std::vector<unsigned char> blob;
    BOOST_CHECK(!CheckAuxPowHeader(a, pqch_hash, chain_id, blob, error));
    BOOST_CHECK_EQUAL(error, "more than one merge mining tag");
}

// Monero hard fork 17 (FCMP++, Carrot): Carrot coinbase outputs, two FCMP++ values at the end of the
// block blob that are leaves 1 and 2 of the transaction tree, and a hashing blob that still counts
// only the real transactions. The expected values are computed here independently of the parser,
// from the layout in P2Pool v5 (block_template.cpp: m_transactionHashes, get_hashing_blob).
BOOST_AUTO_TEST_CASE(monero_fcmp_pp_block_layout)
{
    const uint256 chain_id{Tagged(0xc1)};
    const uint256 pqch_hash{Tagged(0xb1)};
    const uint32_t n_chains{2}, mm_nonce{777};
    AuxTree tree{chain_id, pqch_hash, n_chains, mm_nonce};
    for (size_t n_txs : {0, 1, 2, 5, 6}) {
        MoneroBlock mb;
        mb.major = 17;
        mb.outputs = 3;
        mb.extra = MakeExtra(n_chains, mm_nonce, tree.Root());
        for (size_t i = 0; i < n_txs; ++i) mb.tx_hashes.push_back(Tagged(0x40 + i));

        CAuxPow a;
        CAuxPowCoinbaseHead head;
        std::string error;
        BOOST_REQUIRE_MESSAGE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), tree.Branch(), a, head, error), error);
        BOOST_CHECK_EQUAL(a.tx_count, n_txs + 1);
        BOOST_CHECK_EQUAL(a.TreeLeafCount(), n_txs + 3);

        // Coinbase hash: H(H(prefix) || H(0x00) || zero), the Monero v2 hash with RingCT type null.
        const auto prefix{mb.MinerTxPrefix()};
        unsigned char parts[96]{};
        const uint256 prefix_hash{Keccak256(prefix)};
        const uint256 rct_base{Keccak256(std::array<unsigned char, 1>{0x00})};
        std::memcpy(parts, prefix_hash.begin(), 32);
        std::memcpy(parts + 32, rct_base.begin(), 32);
        const uint256 coinbase_hash{Keccak256(parts)};
        BOOST_CHECK(a.CoinbaseHash() == coinbase_hash);

        // Tree: coinbase, layer count (first byte of a zero hash), FCMP++ root, then the transactions.
        uint256 layers;
        layers.begin()[0] = mb.fcmp_pp_layers;
        std::vector<uint256> leaves{coinbase_hash, layers, mb.fcmp_pp_root};
        leaves.insert(leaves.end(), mb.tx_hashes.begin(), mb.tx_hashes.end());
        std::vector<unsigned char> expected{mb.Header()};
        const uint256 root{MoneroTreeHash(leaves)};
        expected.insert(expected.end(), root.begin(), root.end());
        WriteMoneroVarInt(expected, n_txs + 1); // real transactions only
        BOOST_CHECK(a.HashingBlob() == expected);

        std::vector<unsigned char> blob;
        BOOST_CHECK_MESSAGE(CheckAuxPowHeader(a, pqch_hash, chain_id, blob, error), error);
        BOOST_CHECK(blob == expected);
        BOOST_CHECK_MESSAGE(CheckAuxPowCoinbase(a, head, error), error);

        // A changed FCMP++ value changes the work: the proof is bound to it.
        MoneroBlock other{mb};
        other.fcmp_pp_root = Tagged(0x78);
        CAuxPow b;
        CAuxPowCoinbaseHead hb;
        BOOST_REQUIRE(BuildFromMoneroBlock(other.Serialize(), Tagged(0x5e), tree.Branch(), b, hb, error));
        BOOST_CHECK(b.HashingBlob() != expected);

        // Read as a pre-fork block (major version 16) the same proof no longer fits the tree.
        CAuxPow old{a};
        old.parent_header[0] = 16;
        BOOST_CHECK(old.HashingBlob().empty() || old.HashingBlob() != expected);
    }

    std::string error;
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    // Hard fork 17 block blob without the FCMP++ values: refused.
    {
        MoneroBlock mb;
        mb.major = 17;
        mb.extra = MakeExtra(1, 0, Tagged(0x01));
        auto blob{mb.Serialize()};
        blob.resize(blob.size() - 33);
        BOOST_CHECK(!BuildFromMoneroBlock(blob, Tagged(0x5e), {}, a, head, error));
        BOOST_CHECK_EQUAL(error, "bad Monero transaction list");
    }
    // Pre-fork block with the FCMP++ values or with Carrot outputs: refused.
    {
        MoneroBlock mb;
        mb.extra = MakeExtra(1, 0, Tagged(0x01));
        auto blob{mb.Serialize()};
        blob.push_back(7);
        BOOST_CHECK(!BuildFromMoneroBlock(blob, Tagged(0x5e), {}, a, head, error));
        mb.carrot_outputs = true;
        BOOST_CHECK(!BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), {}, a, head, error));
        BOOST_CHECK_EQUAL(error, "bad Monero output");
    }
    // Carrot outputs checked against the header's version at block level too.
    {
        MoneroBlock mb;
        mb.major = 17;
        mb.extra = MakeExtra(1, 0, Tagged(0x01));
        BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), {}, a, head, error));
        BOOST_CHECK_MESSAGE(CheckAuxPowCoinbase(a, head, error), error);
        CAuxPow old{a};
        old.parent_header[0] = 16;
        BOOST_CHECK(!CheckAuxPowCoinbase(old, head, error));
        BOOST_CHECK_EQUAL(error, "unsupported coinbase output type");
    }
}

// A real Monero block from the FCMP++/Carrot stressnet (major version 18, height 3128295), mined
// locally with P2Pool v5.0-beta and monerod v0.19.0.0-beta.3.0 on 9 Nov. 2026. monerod reported its id
// and proof-of-work hash (get_last_block_header with fill_pow_hash). The parser must rebuild the same
// hashing blob (same block id), and the proof of work from hard fork 17 is the RandomX v2 commitment,
// Blake2b(hashing blob || RandomX v2 hash): plain RandomX v2 made every block "high-hash".
BOOST_AUTO_TEST_CASE(monero_stressnet_real_block)
{
    const auto blob{ParseHex(
        "1212a4c8c8d7066d671cbc5b2e270b9b7b152d78bbc6a0f82c9bb78ec203bea5b8750f03acee9ac6f2ffff02a3f8be0101ff"
        "e7f7be010180e0a596bb11017669c0d166007e57e7927b62f28bed8c6483381e35a20f8d9d34f17f718af4f3409af1282f2f"
        "1dc6e378470b0db3f6a307383a4a0155256841ae186972be0eb54af6f5b92036640e785140b76a585613a9a3f1913302042c"
        "935d7e0321088b8360bc4e4c2e611fc892751037f5f27136013a6b17eba90f39a53cfed77dc6000005683ab832ab6c0221ff"
        "80239b9e305eac5c53809b653c3bdcd29742ea32aeb582")};
    const auto seed_bytes{ParseHex("48afc8fad68ef07ae567a18ea65f2bbd945c80d833a03e7e7546c340444f2b4d")}; // block 3127296
    uint256 seed;
    std::copy(seed_bytes.begin(), seed_bytes.end(), seed.begin());
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    std::string error;
    BOOST_REQUIRE_MESSAGE(BuildFromMoneroBlock(blob, seed, {}, a, head, error), error);
    BOOST_CHECK_EQUAL(a.tx_count, 1U);
    BOOST_CHECK_EQUAL(a.TreeLeafCount(), 3U);
    BOOST_CHECK_MESSAGE(CheckAuxPowCoinbase(a, head, error), error);
    const auto hashing_blob{a.HashingBlob()};
    std::vector<unsigned char> id_data;
    WriteMoneroVarInt(id_data, hashing_blob.size());
    id_data.insert(id_data.end(), hashing_blob.begin(), hashing_blob.end());
    BOOST_CHECK_EQUAL(HexStr(Keccak256(id_data)), "50a510000ad827d877aa74488b968c4ff5626e9a125c04bd743db8765c27818f");
    BOOST_CHECK_EQUAL(HexStr(MoneroPowHash(seed, hashing_blob)), "5a3aa367f7530c98c1813f5ffc1b6e1c36d46a61c53705bdf7eb25a1764f0000");
}

BOOST_AUTO_TEST_CASE(auxpow_coinbase_head)
{
    // A P2Pool-like coinbase with many outputs: most of the prefix goes into the midstate.
    const uint256 chain_id{Tagged(0xc1)};
    const uint256 pqch_hash{Tagged(0xb1)};
    AuxTree tree{chain_id, pqch_hash, 1, 0};
    MoneroBlock mb;
    mb.outputs = 200;
    mb.extra = MakeExtra(1, 0, tree.Root());
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    std::string error;
    BOOST_REQUIRE_MESSAGE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), {}, a, head, error), error);
    BOOST_CHECK(a.head_size > 50 * KECCAK_RATE);
    BOOST_CHECK(a.coinbase_tail.size() < KECCAK_RATE);
    BOOST_CHECK(Serialized(a).size() < 600); // the header stays small
    BOOST_CHECK(Serialized(a).size() <= MAX_AUXPOW_SIZE);
    std::vector<unsigned char> blob;
    BOOST_CHECK_MESSAGE(CheckAuxPowHeader(a, pqch_hash, chain_id, blob, error), error);
    BOOST_CHECK_MESSAGE(CheckAuxPowCoinbase(a, head, error), error);

    // Wrong head bytes, or a head of another length: refused.
    CAuxPowCoinbaseHead bad_head{head};
    bad_head.head[10] ^= 1;
    BOOST_CHECK(!CheckAuxPowCoinbase(a, bad_head, error));
    bad_head = head;
    bad_head.head.resize(head.head.size() - KECCAK_RATE);
    BOOST_CHECK(!CheckAuxPowCoinbase(a, bad_head, error));

    // The attack GHSA-3qmx-q9pv-f3m4 is about: a "coinbase" whose midstate hides a second merge
    // mining tag (here: the prefix is not a canonical coinbase and does not end at tx_extra).
    // Build the midstate over bytes that are not a coinbase prefix; the header check alone passes,
    // the block check does not.
    std::vector<unsigned char> fake(2 * KECCAK_RATE, 0x03);
    CKeccak256 k;
    k.Write(fake);
    CAuxPow forged{a};
    forged.head_size = fake.size();
    forged.coinbase_midstate = k.GetState();
    CAuxPowCoinbaseHead forged_head{fake};
    BOOST_CHECK(CheckAuxPowHeader(forged, pqch_hash, chain_id, blob, error));
    BOOST_CHECK(!CheckAuxPowCoinbase(forged, forged_head, error));

    // Moving bytes between head and tail (a split off the 136-byte boundary) is refused.
    CAuxPow shifted{a};
    shifted.head_size = a.head_size - KECCAK_RATE;
    BOOST_CHECK(!CheckAuxPowCoinbase(shifted, head, error));

    // Bounds.
    CAuxPow big{a};
    big.coinbase_extra.resize(MAX_COINBASE_EXTRA_SIZE + 1);
    BOOST_CHECK(!CheckAuxPowHeader(big, pqch_hash, chain_id, blob, error));
    big = a;
    big.coinbase_tail.resize(KECCAK_RATE);
    BOOST_CHECK(!CheckAuxPowHeader(big, pqch_hash, chain_id, blob, error));
    big = a;
    big.head_size += 1;
    BOOST_CHECK(!CheckAuxPowHeader(big, pqch_hash, chain_id, blob, error));
}

BOOST_AUTO_TEST_CASE(auxpow_block_serialization)
{
    CBlock block;
    block.nVersion = 0x20000000 | VERSION_AUXPOW;
    block.hashPrevBlock = Tagged(1);
    block.hashMerkleRoot = Tagged(2);
    block.nTime = 1'790'000'000;
    block.nBits = 0x207fffff;
    block.nNonce = 0;
    const uint256 id{block.GetHash()};

    MoneroBlock mb;
    mb.outputs = 50;
    mb.extra = MakeExtra(1, 0, id);
    auto a{std::make_shared<CAuxPow>()};
    std::string error;
    BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), Tagged(0x5e), {}, *a, block.m_auxpow_head, error));
    block.auxpow = a;
    // The proof is not part of the block id.
    BOOST_CHECK(block.GetHash() == id);
    BOOST_CHECK_EQUAL(block.SerializePure().size(), 80U);

    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vout.resize(1);
    block.vtx.push_back(MakeTransactionRef(cb));

    DataStream s;
    s << TX_WITH_WITNESS(block);
    CBlock read;
    s >> TX_WITH_WITNESS(read);
    BOOST_CHECK(s.empty());
    BOOST_CHECK(read.GetHash() == id);
    BOOST_REQUIRE(read.auxpow);
    BOOST_CHECK(Serialized(*read.auxpow) == Serialized(*a));
    BOOST_CHECK(read.m_auxpow_head.head == block.m_auxpow_head.head);

    // A header alone (headers message) carries the proof but not the coinbase head.
    DataStream hs;
    hs << static_cast<const CBlockHeader&>(block);
    BOOST_CHECK_EQUAL(hs.size(), 80 + Serialized(*a).size());
    // A header with the bit and no proof cannot be serialized.
    CBlockHeader no_proof{static_cast<const CBlockHeader&>(block)};
    no_proof.auxpow.reset();
    DataStream ns;
    BOOST_CHECK_THROW(ns << no_proof, std::ios_base::failure);

    // A non-auxpow block is unchanged (80-byte header).
    CBlock plain;
    plain.nVersion = 0x20000000;
    plain.vtx.push_back(MakeTransactionRef(cb));
    DataStream ps;
    ps << static_cast<const CBlockHeader&>(plain);
    BOOST_CHECK_EQUAL(ps.size(), 80U);
}

BOOST_AUTO_TEST_CASE(auxpow_seed_rule)
{
    Consensus::Params params;
    params.nAuxSeedWindow = 100;
    params.nAuxNewSeedsPerWindow = 2;

    // A chain where some blocks are merged mined with given seeds.
    std::deque<CBlockIndex> chain;
    std::vector<uint256> hashes;
    auto add{[&](std::optional<uint256> seed) -> CBlockIndex& {
        CBlockIndex& idx{chain.emplace_back()};
        idx.nHeight = chain.size() - 1;
        idx.pprev = chain.size() > 1 ? &chain[chain.size() - 2] : nullptr;
        if (seed) {
            auto a{std::make_shared<CAuxPow>()};
            a->seed_hash = *seed;
            idx.m_auxpow = a;
        }
        SetLastNewAuxSeed(idx, params);
        return idx;
    }};
    add(std::nullopt); // genesis
    bool is_new;
    std::string error;

    BOOST_CHECK(CheckAuxSeed(Tagged(1), &chain.back(), params, is_new, error) && is_new);
    add(Tagged(1));
    BOOST_CHECK(chain.back().m_last_new_aux_seed == &chain.back());
    for (int i = 0; i < 10; ++i) add(Tagged(1)); // reuse: not new
    BOOST_CHECK(chain.back().m_last_new_aux_seed == &chain[1]);
    BOOST_CHECK(CheckAuxSeed(Tagged(1), &chain.back(), params, is_new, error) && !is_new);
    add(Tagged(2));
    add(std::nullopt); // a direct RandomX block in between
    // Two new seeds in the window: a third is refused, the known ones are fine.
    BOOST_CHECK(!CheckAuxSeed(Tagged(3), &chain.back(), params, is_new, error));
    BOOST_CHECK_EQUAL(error, "auxpow-too-many-seeds");
    BOOST_CHECK(CheckAuxSeed(Tagged(2), &chain.back(), params, is_new, error) && !is_new);
    BOOST_CHECK(CheckAuxSeed(Tagged(1), &chain.back(), params, is_new, error) && !is_new);

    // Once the first new seed leaves the window, a new one is allowed again, and seed 1 (introduced
    // more than a window ago) counts as new.
    while (chain.back().nHeight < 100) add(std::nullopt);
    BOOST_CHECK(CheckAuxSeed(Tagged(3), &chain.back(), params, is_new, error) && is_new);
    BOOST_CHECK(CheckAuxSeed(Tagged(1), &chain.back(), params, is_new, error) && is_new);
    add(Tagged(3));
    BOOST_CHECK(!CheckAuxSeed(Tagged(4), &chain.back(), params, is_new, error));
}

BOOST_AUTO_TEST_SUITE_END()

/** End to end on regtest: a merged mined block is accepted; tampered variants are not. */
BOOST_FIXTURE_TEST_SUITE(pqch_auxpow_chain_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(merged_mined_block_accepted)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const Consensus::Params& params{chainman.GetConsensus()};
    const uint256 chain_id{params.hashGenesisBlock};
    const uint256 seed{Tagged(0x5e)};

    CBlock block{CreateBlock({}, m_coinbase_script)};
    block.nVersion |= VERSION_AUXPOW;
    block.nNonce = 0;
    const uint256 id{block.GetHash()};

    // A Monero miner merge mining PQCH with two other chains, grinding its own nonce.
    const uint32_t n_chains{3}, mm_nonce{42};
    AuxTree tree{chain_id, id, n_chains, mm_nonce};
    MoneroBlock mb;
    mb.outputs = 30;
    mb.extra = MakeExtra(n_chains, mm_nonce, tree.Root());
    mb.tx_hashes = {Tagged(0x61), Tagged(0x62)};
    auto a{std::make_shared<CAuxPow>()};
    std::string error;
    const arith_uint256 target{arith_uint256{}.SetCompact(block.nBits)};
    for (;; ++mb.nonce) {
        BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), seed, tree.Branch(), *a, block.m_auxpow_head, error));
        const auto blob{a->HashingBlob()};
        if (UintToArith256(MoneroPowHash(seed, blob)) <= target) break;
    }
    block.auxpow = a;
    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(CheckBlockProofOfWork(block, tip, params, error));

    // The header arrives first (headers-first sync), with its proof.
    {
        BlockValidationState state;
        BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({{static_cast<const CBlockHeader&>(block)}}, /*min_pow_checked=*/true, state));
    }

    // The block without the coinbase head (bad data, not a bad block) is refused but not marked invalid.
    {
        auto stripped{std::make_shared<CBlock>(block)};
        stripped->m_auxpow_head.head.clear();
        BlockValidationState state;
        BOOST_CHECK(!CheckBlock(*stripped, state, params));
        BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_MUTATED);
        bool new_block;
        BOOST_CHECK(!chainman.ProcessNewBlock(stripped, true, true, &new_block));
        const CBlockIndex* idx{WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(id))};
        BOOST_REQUIRE(idx);
        BOOST_CHECK(!(idx->nStatus & BLOCK_FAILED_VALID));
    }

    // Another proof for the same id (commits to it; on the main network it would also need the work)
    // attached to the known header: refused, the indexed header's proof is the one that counts.
    {
        auto forged{std::make_shared<CBlock>(block)};
        auto fa{std::make_shared<CAuxPow>(*a)};
        MoneroBlock other{mb};
        other.nonce = 0xffffffff;
        BOOST_REQUIRE(BuildFromMoneroBlock(other.Serialize(), seed, tree.Branch(), *fa, forged->m_auxpow_head, error));
        forged->auxpow = fa;
        BOOST_CHECK(!chainman.ProcessNewBlock(forged, true, true, nullptr));
        const CBlockIndex* idx{WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(id))};
        BOOST_CHECK(!(idx->nStatus & (BLOCK_FAILED_VALID | BLOCK_HAVE_DATA)));
    }

    // The real block: accepted and becomes the tip.
    BOOST_CHECK(chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), true, true, nullptr));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainman.ActiveChain().Tip()->GetBlockHash()) == id);
    const CBlockIndex* idx{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(idx->m_auxpow);
    BOOST_CHECK(idx->AuxSeed() == seed);
    BOOST_CHECK(idx->m_last_new_aux_seed == idx);

    // Read back from disk with its proof and head.
    CBlock from_disk;
    BOOST_REQUIRE(chainman.m_blockman.ReadBlock(from_disk, *idx));
    BOOST_REQUIRE(from_disk.auxpow);
    BOOST_CHECK(Serialized(*from_disk.auxpow) == Serialized(*a));
    BOOST_CHECK(from_disk.m_auxpow_head.head == block.m_auxpow_head.head);

    // getblockheader / getblock show the proof (Monero hashes in Monero's byte order).
    {
        const UniValue json{blockheaderToJSON(*idx, *idx, params.powLimit)};
        const UniValue& aux{json.find_value("auxpow")};
        BOOST_REQUIRE(aux.isObject());
        BOOST_CHECK_EQUAL(aux.find_value("seed_hash").get_str(), HexStr(seed));
        BOOST_CHECK_EQUAL(aux.find_value("parent_header").get_str(), HexStr(a->parent_header));
        BOOST_CHECK_EQUAL(aux.find_value("coinbase_extra").get_str(), HexStr(a->coinbase_extra));
        BOOST_CHECK_EQUAL(aux.find_value("aux_branch").size(), a->aux_branch.size());
        BOOST_CHECK_EQUAL(aux.find_value("tx_count").getInt<uint64_t>(), 3U);
        BOOST_CHECK(blockheaderToJSON(*idx, *Assert(idx->pprev), params.powLimit).find_value("auxpow").isNull());
    }

    // A normal block can follow.
    CreateAndProcessBlock({}, m_coinbase_script);
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return chainman.ActiveChain().Height()), 102);
}

// A merged mined header that brings a Monero key no valid header has used costs a 256 MiB RandomX
// cache (~0.5 s, under cs_main) before its work can be checked, and anyone can send one with fake
// work. Such headers are only checked when a peer we chose (outbound) or trust (NoBan) sends them;
// from an inbound peer they are ignored without penalty and without building the cache.
BOOST_AUTO_TEST_CASE(new_monero_key_only_checked_from_chosen_peers)
{
    LOCK(NetEventsInterface::g_msgproc_mutex);
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const Consensus::Params& params{chainman.GetConsensus()};
    auto& connman{static_cast<ConnmanTestMsg&>(*m_node.connman)};

    // A merged mined block on the tip under `seed`, whose Monero work meets the target or not.
    uint32_t block_nonce{0};
    auto make_block{[&](const uint256& seed, bool valid_work) {
        CBlock block{CreateBlock({}, m_coinbase_script)};
        block.nVersion |= VERSION_AUXPOW;
        block.nNonce = ++block_nonce; // the block id does not commit to the proof: make each one different
        AuxTree tree{params.hashGenesisBlock, block.GetHash(), 1, 7};
        MoneroBlock mb;
        mb.extra = MakeExtra(1, 7, tree.Root());
        auto a{std::make_shared<CAuxPow>()};
        std::string error;
        const arith_uint256 target{arith_uint256{}.SetCompact(block.nBits)};
        for (;; ++mb.nonce) {
            BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), seed, tree.Branch(), *a, block.m_auxpow_head, error));
            if ((UintToArith256(MoneroPowHash(seed, a->HashingBlob())) <= target) == valid_work) break;
        }
        block.auxpow = a;
        return block;
    }};
    // Grinding above built the caches of these keys: start each case with a key not cached.
    auto forget_keys{[&]() {
        for (uint8_t t = 0xd0; t < 0xd8; ++t) RandomXHash(std::span<const unsigned char>{Tagged(t).data(), 32}, std::span<const unsigned char>{});
    }};
    auto key_cached{[](const uint256& seed) { return RandomXKeyCached(std::span<const unsigned char>{seed.data(), seed.size()}); }};
    auto punished{[&](const CNode& node) { return m_node.banman->IsDiscouraged(node.addr); }};
    auto known{[&](const CBlockHeader& h) { return WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(h.GetHash()) != nullptr); }};

    NodeId id{0};
    std::vector<std::unique_ptr<CNode>> peers;
    auto make_peer{[&](ConnectionType type, NetPermissionFlags permissions = NetPermissionFlags::None) -> CNode& {
        const CAddress addr{Lookup(strprintf("1.2.3.%d", 10 + id), 8333, /*fAllowLookup=*/false).value(), NODE_NETWORK};
        auto& node{*peers.emplace_back(std::make_unique<CNode>(id++, /*sock=*/nullptr, addr, /*nKeyedNetGroupIn=*/0, /*nLocalHostNonceIn=*/0,
                                                               CService{}, /*addrNameIn=*/"", type, /*inbound_onion=*/false, /*network_key=*/0,
                                                               CNodeOptions{.permission_flags = permissions}))};
        connman.Handshake(node, /*successfully_connected=*/true, ServiceFlags(NODE_NETWORK | NODE_WITNESS), NODE_NETWORK, PROTOCOL_VERSION, /*relay_txs=*/true);
        return node;
    }};
    auto send_headers{[&](CNode& node, const CBlockHeader& header) {
        DataStream s;
        WriteCompactSize(s, 1);
        s << header;
        WriteCompactSize(s, 0);
        CSerializedNetMsg msg;
        msg.m_type = NetMsgType::HEADERS;
        msg.data.resize(s.size());
        std::memcpy(msg.data.data(), s.data(), s.size());
        connman.FlushSendBuffer(node);
        node.fPauseSend = false; // the test reads nothing the node sends
        BOOST_REQUIRE(connman.ReceiveMsgFrom(node, std::move(msg)));
        connman.ProcessMessagesOnce(node);
        m_node.peerman->SendMessages(node); // a misbehaving peer is discouraged here
    }};

    CNode& inbound{make_peer(ConnectionType::INBOUND)};
    CNode& outbound{make_peer(ConnectionType::OUTBOUND_FULL_RELAY)};

    // Fake work, new key, from an inbound peer: not checked (no cache), not stored, no penalty.
    const uint256 key_a{Tagged(0xc1)};
    const CBlock fake{make_block(key_a, /*valid_work=*/false)};
    forget_keys();
    BOOST_REQUIRE(!key_cached(key_a));
    send_headers(inbound, fake);
    BOOST_CHECK(!key_cached(key_a));
    BOOST_CHECK(!known(fake));
    BOOST_CHECK(!punished(inbound));

    // The same header from an outbound peer is checked: refused, and the peer is punished.
    send_headers(outbound, fake);
    BOOST_CHECK(key_cached(key_a));
    BOOST_CHECK(!known(fake));
    BOOST_CHECK(punished(outbound));

    // A real block with a new key: waits for an outbound peer, then is accepted.
    CNode& inbound2{make_peer(ConnectionType::INBOUND)};
    CNode& outbound2{make_peer(ConnectionType::OUTBOUND_FULL_RELAY)};
    const uint256 key_b{Tagged(0xc2)};
    const CBlock real{make_block(key_b, /*valid_work=*/true)};
    forget_keys();
    send_headers(inbound2, real);
    BOOST_CHECK(!known(real));
    BOOST_CHECK(!key_cached(key_b));
    send_headers(outbound2, real);
    BOOST_CHECK(known(real));
    BOOST_CHECK(!punished(outbound2));
    // Now the key is known: the same header from the inbound peer is fine.
    send_headers(inbound2, real);
    BOOST_CHECK(!punished(inbound2));

    // An unrequested full block with a new key from an inbound peer is ignored as well.
    const uint256 key_c{Tagged(0xc3)};
    const CBlock real_c{make_block(key_c, /*valid_work=*/true)};
    forget_keys();
    connman.FlushSendBuffer(inbound2);
    inbound2.fPauseSend = false;
    BOOST_REQUIRE(connman.ReceiveMsgFrom(inbound2, NetMsg::Make(NetMsgType::BLOCK, TX_WITH_WITNESS(real_c))));
    connman.ProcessMessagesOnce(inbound2);
    BOOST_CHECK(!known(real_c));
    BOOST_CHECK(!key_cached(key_c));

    // A NoBan (trusted) inbound peer is treated like an outbound one.
    CNode& trusted{make_peer(ConnectionType::INBOUND, NetPermissionFlags::NoBan)};
    send_headers(trusted, real_c);
    BOOST_CHECK(known(real_c));

    for (auto& node : peers) m_node.peerman->FinalizeNode(*node);
}

// A PQCH block merge mined with a Monero block after hard fork 17: Carrot coinbase, FCMP++ tree
// leaves, RandomX v2. Full validation through ProcessNewBlock.
BOOST_AUTO_TEST_CASE(merged_mined_block_monero_hf17)
{
    ChainstateManager& chainman{*Assert(m_node.chainman)};
    const Consensus::Params& params{chainman.GetConsensus()};
    const uint256 chain_id{params.hashGenesisBlock};
    const uint256 seed{Tagged(0x5f)};

    CBlock block{CreateBlock({}, m_coinbase_script)};
    block.nVersion |= VERSION_AUXPOW;
    block.nNonce = 0;
    const uint256 id{block.GetHash()};

    const uint32_t n_chains{2}, mm_nonce{9};
    AuxTree tree{chain_id, id, n_chains, mm_nonce};
    MoneroBlock mb;
    mb.major = 17;
    mb.outputs = 12;
    mb.extra = MakeExtra(n_chains, mm_nonce, tree.Root());
    mb.tx_hashes = {Tagged(0x61), Tagged(0x62), Tagged(0x63)};
    auto a{std::make_shared<CAuxPow>()};
    std::string error;
    const arith_uint256 target{arith_uint256{}.SetCompact(block.nBits)};
    for (;; ++mb.nonce) {
        BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), seed, tree.Branch(), *a, block.m_auxpow_head, error));
        if (UintToArith256(MoneroPowHash(seed, a->HashingBlob())) <= target) break;
    }
    block.auxpow = a;
    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE_MESSAGE(CheckBlockProofOfWork(block, tip, params, error), error);
    BOOST_CHECK(chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), true, true, nullptr));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainman.ActiveChain().Tip()->GetBlockHash()) == id);
}

BOOST_AUTO_TEST_CASE(p2pool_merge_mining_api)
{
    // The calls P2Pool v4 makes (merge_mining_client_json_rpc.cpp), end to end.
    m_node.mining = interfaces::MakeMining(m_node);
    node::MergeMiningServer server{m_node};
    // While the node starts up the API answers with an error.
    UniValue warm;
    BOOST_REQUIRE(warm.read(server.HandleRequest(R"({"jsonrpc":"2.0","id":"0","method":"merge_mining_get_chain_id"})")));
    if (RPCIsInWarmup(nullptr)) {
        BOOST_CHECK(warm["error"].isObject());
        SetRPCWarmupFinished();
    }
    const auto call{[&](const std::string& body) {
        UniValue r;
        BOOST_REQUIRE(r.read(server.HandleRequest(body)));
        BOOST_CHECK_EQUAL(r["jsonrpc"].get_str(), "2.0");
        return r;
    }};
    const Consensus::Params& params{m_node.chainman->GetConsensus()};

    UniValue r{call(R"({"jsonrpc":"2.0","id":"0","method":"merge_mining_get_chain_id"})")};
    BOOST_CHECK_EQUAL(r["result"]["chain_id"].get_str(), HexStr(params.hashGenesisBlock));
    BOOST_CHECK(call("not json")["error"].isObject());
    BOOST_CHECK(call(R"({"jsonrpc":"2.0","id":"0","method":"getblockcount"})")["error"].isObject());

    // Only post-quantum (P2MR) payout addresses.
    r = call(R"({"jsonrpc":"2.0","id":"0","method":"merge_mining_get_aux_block","params":{"address":"nonsense","aux_hash":"0000000000000000000000000000000000000000000000000000000000000000","height":3000000,"prev_id":"0000000000000000000000000000000000000000000000000000000000000000"}})");
    BOOST_CHECK(r["error"].isObject());

    const uint256 key_bytes{Tagged(0x77)};
    const std::string address{EncodeDestination(WitnessV1Taproot{XOnlyPubKey{std::span<const unsigned char>{key_bytes.begin(), 32}}})};
    const std::string get_job{R"({"jsonrpc":"2.0","id":"0","method":"merge_mining_get_aux_block","params":{"address":")" + address +
                              R"(","aux_hash":"0000000000000000000000000000000000000000000000000000000000000000","height":3000000,"prev_id":"0000000000000000000000000000000000000000000000000000000000000000"}})"};
    r = call(get_job);
    BOOST_REQUIRE_MESSAGE(r["result"].isObject(), r.write());
    const std::string aux_hash_hex{r["result"]["aux_hash"].get_str()};
    BOOST_CHECK_EQUAL(r["result"]["aux_blob"].get_str().size(), 160U);
    BOOST_CHECK(r["result"]["aux_diff"].getInt<uint64_t>() >= 1);
    // The same job while the tip does not change.
    BOOST_CHECK_EQUAL(call(get_job)["result"]["aux_hash"].get_str(), aux_hash_hex);

    // A Monero miner finds a block meeting aux_diff and P2Pool submits it.
    uint256 aux_hash;
    const auto aux_hash_bytes{ParseHex(aux_hash_hex)};
    std::copy(aux_hash_bytes.begin(), aux_hash_bytes.end(), aux_hash.begin());
    const uint32_t n_chains{2}, mm_nonce{7};
    AuxTree tree{params.hashGenesisBlock, aux_hash, n_chains, mm_nonce};
    MoneroBlock mb;
    mb.outputs = 12;
    mb.extra = MakeExtra(n_chains, mm_nonce, tree.Root());
    const uint256 seed{Tagged(0x5f)};
    const uint32_t nbits{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->nBits)};
    const arith_uint256 target{arith_uint256{}.SetCompact(nbits)};
    for (;; ++mb.nonce) {
        CAuxPow a;
        CAuxPowCoinbaseHead h;
        std::string error;
        BOOST_REQUIRE(BuildFromMoneroBlock(mb.Serialize(), seed, tree.Branch(), a, h, error));
        if (UintToArith256(MoneroPowHash(seed, a.HashingBlob())) <= target) break;
    }
    std::string proof{"["};
    for (const auto& h : tree.Branch()) proof += (proof.size() > 1 ? ",\"" : "\"") + HexStr(h) + "\"";
    proof += "]";
    const std::string submit{R"({"jsonrpc":"2.0","id":"0","method":"merge_mining_submit_solution","params":{"aux_blob":")" +
                             r["result"]["aux_blob"].get_str() + R"(","aux_hash":")" + aux_hash_hex + R"(","blob":")" + HexStr(mb.Serialize()) +
                             R"(","merkle_proof":)" + proof + R"(,"path":)" + std::to_string(tree.slot) + R"(,"seed_hash":")" + HexStr(seed) + R"("}})"};
    r = call(submit);
    BOOST_REQUIRE_MESSAGE(r["result"].isObject(), r.write());
    BOOST_CHECK_EQUAL(r["result"]["status"].get_str(), "accepted");
    BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash()) == aux_hash);

    // A new tip gives a new job; an unknown aux_hash is refused.
    BOOST_CHECK(call(get_job)["result"]["aux_hash"].get_str() != aux_hash_hex);
    std::string unknown{submit};
    unknown.replace(unknown.find(aux_hash_hex), 64, std::string(64, 'a'));
    BOOST_CHECK(call(unknown)["error"].isObject());
}

BOOST_AUTO_TEST_SUITE_END()
