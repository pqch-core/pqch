// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH fuzz targets: the new code that reads data from the network (merged mining proofs, post-
// quantum signatures) and the post-quantum wallet descriptor.

#include <auxpow.h>
#include <chainparams.h>
#include <crypto/pqc/pqc.h>
#include <key.h>
#include <primitives/block.h>
#include <script/descriptor.h>
#include <script/p2mr.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <util/check.h>
#include <util/strencodings.h>

#include <array>
#include <cassert>
#include <span>
#include <string>
#include <vector>

using namespace auxpow;

namespace {
void initialize_pqch()
{
    static ECC_Context ecc_context{};
    SelectParams(ChainType::MAIN);
}

std::vector<unsigned char> Serialized(const CAuxPow& a)
{
    DataStream s;
    s << a;
    const auto b{MakeUCharSpan(s)};
    return {b.begin(), b.end()};
}
} // namespace

/** Header (with or without merged mining proof) from network bytes: no crash, stable round trip,
 *  the header checks never crash on any proof. */
FUZZ_TARGET(pqch_auxpow_header, .init = initialize_pqch)
{
    FuzzedDataProvider fdp{buffer.data(), buffer.size()};
    const auto bytes{fdp.ConsumeRemainingBytes<unsigned char>()};
    DataStream ds{bytes};
    CBlockHeader header;
    try {
        ds >> header;
    } catch (const std::ios_base::failure&) {
        return;
    }
    assert(header.IsAuxPow() == bool{header.auxpow});
    DataStream again;
    again << header;
    CBlockHeader header2;
    again >> header2;
    assert(header2.GetHash() == header.GetHash());
    if (!header.auxpow) return;
    assert(Serialized(*header2.auxpow) == Serialized(*header.auxpow));
    std::vector<unsigned char> blob;
    std::string error;
    const bool ok{CheckAuxPowHeader(*header.auxpow, header.GetHash(), Params().GetConsensus().hashGenesisBlock, blob, error)};
    if (ok) assert(!blob.empty());
    // Any head bytes: never a crash; the empty head only matches a proof without one.
    CAuxPowCoinbaseHead head;
    head.head = std::vector<unsigned char>(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 3 * KECCAK_RATE));
    (void)CheckAuxPowCoinbase(*header.auxpow, head, error);
}

/** A full Monero block (what P2Pool submits): whatever BuildFromMoneroBlock accepts must pass the
 *  block-level coinbase check and give a hashing blob. */
FUZZ_TARGET(pqch_auxpow_monero_block, .init = initialize_pqch)
{
    FuzzedDataProvider fdp{buffer.data(), buffer.size()};
    const uint256 seed{ConsumeUInt256(fdp)};
    std::vector<uint256> branch;
    const size_t n{fdp.ConsumeIntegralInRange<size_t>(0, MAX_AUX_BRANCH)};
    for (size_t i = 0; i < n; ++i) branch.push_back(ConsumeUInt256(fdp));
    const auto block{fdp.ConsumeRemainingBytes<unsigned char>()};
    CAuxPow a;
    CAuxPowCoinbaseHead head;
    std::string error;
    if (!BuildFromMoneroBlock(block, seed, branch, a, head, error)) return;
    assert(CheckAuxPowCoinbase(a, head, error));
    assert(!a.HashingBlob().empty());
    // The tx_extra parser never crashes on what the block carried.
    MergeMiningTag tag;
    (void)ParseCoinbaseExtra(a.coinbase_extra, tag, error);
}

/** Monero tree, proofs, merge mining parameters, varints and Keccak streaming. */
FUZZ_TARGET(pqch_monero_primitives, .init = initialize_pqch)
{
    FuzzedDataProvider fdp{buffer.data(), buffer.size()};
    // Tree and proofs: every leaf's proof leads to the root; another index does not need to.
    std::vector<uint256> leaves;
    const size_t n{fdp.ConsumeIntegralInRange<size_t>(1, 70)};
    for (size_t i = 0; i < n; ++i) leaves.push_back(ConsumeUInt256(fdp));
    const uint64_t index{fdp.ConsumeIntegralInRange<uint64_t>(0, n - 1)};
    uint256 root;
    assert(MoneroRootFromProof(leaves[index], MoneroProof(leaves, index), index, n, root));
    assert(root == MoneroTreeHash(leaves));
    // Arbitrary proof data never crashes.
    std::vector<uint256> proof;
    for (size_t i = fdp.ConsumeIntegralInRange<size_t>(0, 25); i > 0; --i) proof.push_back(ConsumeUInt256(fdp));
    (void)MoneroRootFromProof(ConsumeUInt256(fdp), proof, fdp.ConsumeIntegral<uint64_t>(), fdp.ConsumeIntegral<uint64_t>(), root);

    // Merge mining parameters: decoding is the inverse of encoding, and only canonical values decode.
    const uint64_t v{fdp.ConsumeIntegral<uint64_t>()};
    uint32_t chains, nonce;
    if (DecodeMergeMiningParams(v, chains, nonce)) assert(EncodeMergeMiningParams(chains, nonce) == v);
    const uint32_t c2{fdp.ConsumeIntegralInRange<uint32_t>(1, 256)}, n2{fdp.ConsumeIntegral<uint32_t>()};
    assert(DecodeMergeMiningParams(EncodeMergeMiningParams(c2, n2), chains, nonce) && chains == c2 && nonce == n2);
    assert(AuxSlot(ConsumeUInt256(fdp), n2, c2) < c2);

    // Varints: round trip; parsing arbitrary bytes never reads past the end.
    const uint64_t x{fdp.ConsumeIntegral<uint64_t>()};
    std::vector<unsigned char> enc;
    WriteMoneroVarInt(enc, x);
    std::span<const unsigned char> in{enc};
    uint64_t y;
    assert(ReadMoneroVarInt(in, y) && y == x && in.empty());

    // Keccak: streaming in random pieces equals one shot; resuming from a midstate too.
    const auto data{fdp.ConsumeBytes<unsigned char>(fdp.ConsumeIntegralInRange<size_t>(0, 1000))};
    const uint256 one_shot{Keccak256(data)};
    CKeccak256 k;
    for (size_t i = 0; i < data.size();) {
        const size_t take{std::min<size_t>(data.size() - i, fdp.ConsumeIntegralInRange<size_t>(1, 200))};
        k.Write(std::span{data}.subspan(i, take));
        i += take;
    }
    uint256 streamed;
    k.Finalize(std::span<unsigned char, 32>{streamed.begin(), 32});
    assert(streamed == one_shot);
    const size_t blocks{data.size() / KECCAK_RATE};
    CKeccak256 head;
    head.Write(std::span{data}.first(blocks * KECCAK_RATE));
    CKeccak256 resumed{head.GetState(), blocks * KECCAK_RATE};
    uint256 out;
    resumed.Write(std::span{data}.subspan(blocks * KECCAK_RATE)).Finalize(std::span<unsigned char, 32>{out.begin(), 32});
    assert(out == one_shot);

    // tx_extra parser on arbitrary bytes.
    const auto extra{fdp.ConsumeRemainingBytes<unsigned char>()};
    MergeMiningTag tag;
    std::string error;
    (void)ParseCoinbaseExtra(extra, tag, error);
}

/** Post-quantum signature verification on network data: never crashes; a valid ML-DSA signature
 *  verifies, and any change to signature, message, context or key makes it fail. */
FUZZ_TARGET(pqch_pq_verify, .init = initialize_pqch)
{
    FuzzedDataProvider fdp{buffer.data(), buffer.size()};
    // Arbitrary input to both verifiers (sizes chosen around the real ones).
    const auto rnd_sig{fdp.ConsumeBytes<unsigned char>(fdp.PickValueInArray({size_t{0}, pqc::MLDSA44_SIG_SIZE, pqc::SLHDSA128S_SIG_SIZE, fdp.ConsumeIntegralInRange<size_t>(0, 9000)}))};
    const auto rnd_key{fdp.ConsumeBytes<unsigned char>(fdp.PickValueInArray({pqc::MLDSA44_PUBKEY_SIZE, pqc::SLHDSA128S_PUBKEY_SIZE, fdp.ConsumeIntegralInRange<size_t>(0, 2000)}))};
    const auto msg{fdp.ConsumeBytes<unsigned char>(32)};
    (void)pqc::MLDSA44Verify(rnd_sig, msg, pqc::TX_SIGNATURE_CONTEXT, rnd_key);
    (void)pqc::SLHDSA128sVerify(rnd_sig, msg, pqc::TX_SIGNATURE_CONTEXT, rnd_key);

    // Sign with a fuzzer-chosen key, then change one byte of something.
    std::array<unsigned char, pqc::KEY_SEED_SIZE> seed{};
    const auto sb{fdp.ConsumeBytes<unsigned char>(seed.size())};
    std::copy(sb.begin(), sb.end(), seed.begin());
    pqc::KeyPair key;
    assert(pqc::MLDSA44KeyFromSeed(seed, key));
    std::array<unsigned char, 32> rnd{};
    std::vector<unsigned char> sig;
    assert(pqc::MLDSA44Sign(key.seckey, msg, pqc::TX_SIGNATURE_CONTEXT, rnd, sig));
    assert(pqc::MLDSA44Verify(sig, msg, pqc::TX_SIGNATURE_CONTEXT, key.pubkey));
    std::vector<unsigned char> m2{msg}, ctx{pqc::TX_SIGNATURE_CONTEXT.begin(), pqc::TX_SIGNATURE_CONTEXT.end()}, k2{key.pubkey}, s2{sig};
    const uint8_t flip{static_cast<uint8_t>(fdp.ConsumeIntegralInRange<int>(1, 255))};
    switch (fdp.ConsumeIntegralInRange<int>(0, 3)) {
    case 0: s2[fdp.ConsumeIntegralInRange<size_t>(0, s2.size() - 1)] ^= flip; break;
    case 1: if (m2.empty()) m2.push_back(0); else m2[fdp.ConsumeIntegralInRange<size_t>(0, m2.size() - 1)] ^= flip; break;
    case 2: ctx[fdp.ConsumeIntegralInRange<size_t>(0, ctx.size() - 1)] ^= flip; break;
    case 3: k2[fdp.ConsumeIntegralInRange<size_t>(0, k2.size() - 1)] ^= flip; break;
    }
    assert(!pqc::MLDSA44Verify(s2, m2, ctx, k2));
}

/** p2mr() descriptors: parsing never crashes; a parsed one prints and parses back the same, and
 *  expands deterministically. */
FUZZ_TARGET(pqch_p2mr_descriptor, .init = initialize_pqch)
{
    FuzzedDataProvider fdp{buffer.data(), buffer.size()};
    const std::string inner{fdp.ConsumeRandomLengthString(200)};
    for (const std::string& str : {"p2mr(" + inner + ")", "p2mr(pqseed(" + inner + ")", "p2mr(pqprv(" + HexStr(fdp.ConsumeBytes<unsigned char>(32)) + ")" + inner + ")"}) {
        FlatSigningProvider keys;
        std::string error;
        auto descs{Parse(str, keys, error, /*require_checksum=*/false)};
        if (descs.empty()) continue;
        const std::string printed{descs[0]->ToString()};
        FlatSigningProvider keys2;
        auto again{Parse(printed, keys2, error, /*require_checksum=*/true)};
        assert(again.size() == 1 && again[0]->ToString() == printed);
        if (keys.pq_seeds.empty()) continue;
        std::vector<CScript> s1, s2;
        FlatSigningProvider o1, o2;
        DescriptorCache cache;
        const int pos{fdp.ConsumeIntegralInRange<int>(0, 3)};
        if (descs[0]->Expand(pos, keys, s1, o1, &cache)) {
            assert(descs[0]->ExpandFromCache(pos, cache, s2, o2));
            assert(s1 == s2);
        }
    }
}
