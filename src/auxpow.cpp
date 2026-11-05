// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>

#include <crypto/sha256.h>
#include <crypto/sha3.h>
#include <tinyformat.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>

namespace auxpow {

// ---------------------------------------------------------------------------------------------
// Keccak-256 (Monero cn_fast_hash)

namespace {
void AbsorbBlock(CKeccak256::State& st, const unsigned char* block)
{
    for (size_t i = 0; i < KECCAK_RATE / 8; ++i) {
        uint64_t w{0};
        for (int b = 7; b >= 0; --b) w = (w << 8) | block[i * 8 + b];
        st[i] ^= w;
    }
    uint64_t raw[25];
    std::copy(st.begin(), st.end(), raw);
    KeccakF(raw);
    std::copy(std::begin(raw), std::end(raw), st.begin());
}
} // namespace

CKeccak256::CKeccak256(const State& state, uint64_t absorbed) : m_state{state}, m_absorbed{absorbed}
{
    assert(absorbed % KECCAK_RATE == 0);
}

CKeccak256& CKeccak256::Write(std::span<const unsigned char> data)
{
    while (!data.empty()) {
        const size_t take{std::min(KECCAK_RATE - m_buffer_len, data.size())};
        std::memcpy(m_buffer.data() + m_buffer_len, data.data(), take);
        m_buffer_len += take;
        data = data.subspan(take);
        if (m_buffer_len == KECCAK_RATE) {
            AbsorbBlock(m_state, m_buffer.data());
            m_absorbed += KECCAK_RATE;
            m_buffer_len = 0;
        }
    }
    return *this;
}

void CKeccak256::Finalize(std::span<unsigned char, 32> out)
{
    std::fill(m_buffer.begin() + m_buffer_len, m_buffer.end(), 0);
    m_buffer[m_buffer_len] ^= 0x01; // original Keccak padding (SHA-3 uses 0x06)
    m_buffer[KECCAK_RATE - 1] ^= 0x80;
    AbsorbBlock(m_state, m_buffer.data());
    for (size_t i = 0; i < 4; ++i) {
        for (size_t b = 0; b < 8; ++b) out[i * 8 + b] = static_cast<unsigned char>(m_state[i] >> (8 * b));
    }
}

uint256 Keccak256(std::span<const unsigned char> data)
{
    uint256 out;
    CKeccak256().Write(data).Finalize(std::span<unsigned char, 32>{out.begin(), 32});
    return out;
}

uint256 KeccakPair(const uint256& a, const uint256& b)
{
    unsigned char buf[64];
    std::memcpy(buf, a.begin(), 32);
    std::memcpy(buf + 32, b.begin(), 32);
    return Keccak256(buf);
}

// ---------------------------------------------------------------------------------------------
// Monero tree hash and proofs (crypto/tree-hash.c; P2Pool merkle.cpp)

namespace {
/** Largest power of two not above count (count >= 3 here). */
uint64_t TreeWidth(uint64_t count)
{
    // std::bit_floor: no overflow for any count (a shifting loop never ends above 2^63).
    return std::bit_floor(count);
}
/** Trees larger than this are refused (consensus limits are far smaller: 2^20 transactions). */
constexpr uint64_t MAX_TREE_SIZE{uint64_t{1} << 32};
} // namespace

uint256 MoneroTreeHash(const std::vector<uint256>& hashes)
{
    const uint64_t count{hashes.size()};
    if (count == 0) return uint256{};
    if (count == 1) return hashes[0];
    if (count == 2) return KeccakPair(hashes[0], hashes[1]);
    uint64_t cnt{TreeWidth(count)};
    std::vector<uint256> ints(cnt);
    const uint64_t k{cnt * 2 - count};
    std::copy(hashes.begin(), hashes.begin() + k, ints.begin());
    for (uint64_t i = k, j = k; j < cnt; i += 2, ++j) ints[j] = KeccakPair(hashes[i], hashes[i + 1]);
    while (cnt > 2) {
        cnt >>= 1;
        for (uint64_t i = 0, j = 0; j < cnt; i += 2, ++j) ints[j] = KeccakPair(ints[i], ints[i + 1]);
    }
    return KeccakPair(ints[0], ints[1]);
}

bool MoneroRootFromProof(const uint256& leaf, const std::vector<uint256>& proof, uint64_t index, uint64_t count, uint256& root)
{
    if (count == 0 || index >= count || count > MAX_TREE_SIZE) return false;
    uint256 h{leaf};
    if (count == 1) {
        if (!proof.empty()) return false;
        root = h;
        return true;
    }
    auto combine{[&](const uint256& p, uint64_t idx) { h = (idx & 1) ? KeccakPair(p, h) : KeccakPair(h, p); }};
    if (count == 2) {
        if (proof.size() != 1) return false;
        combine(proof[0], index);
        root = h;
        return true;
    }
    uint64_t cnt{TreeWidth(count)};
    size_t proof_index{0};
    const uint64_t k{cnt * 2 - count};
    if (index >= k) {
        index -= k;
        if (proof.empty()) return false;
        combine(proof[0], index);
        index = (index >> 1) + k;
        proof_index = 1;
    }
    for (; cnt >= 2; ++proof_index, index >>= 1, cnt >>= 1) {
        if (proof_index >= proof.size()) return false;
        combine(proof[proof_index], index);
    }
    if (proof_index != proof.size()) return false;
    root = h;
    return true;
}

std::vector<uint256> MoneroProof(const std::vector<uint256>& hashes, uint64_t index)
{
    const uint64_t count{hashes.size()};
    assert(index < count);
    std::vector<uint256> proof;
    if (count == 1) return proof;
    if (count == 2) {
        proof.push_back(hashes[index ^ 1]);
        return proof;
    }
    uint64_t cnt{TreeWidth(count)};
    const uint64_t k{cnt * 2 - count};
    std::vector<uint256> level(cnt);
    std::copy(hashes.begin(), hashes.begin() + k, level.begin());
    uint64_t pos;
    for (uint64_t i = k, j = k; j < cnt; i += 2, ++j) level[j] = KeccakPair(hashes[i], hashes[i + 1]);
    if (index >= k) {
        const uint64_t rel{index - k};
        proof.push_back(hashes[k + (rel ^ 1)]);
        pos = (rel >> 1) + k;
    } else {
        pos = index;
    }
    while (cnt >= 2) {
        proof.push_back(level[pos ^ 1]);
        cnt >>= 1;
        std::vector<uint256> next(cnt);
        for (uint64_t i = 0, j = 0; j < cnt; i += 2, ++j) next[j] = KeccakPair(level[i], level[i + 1]);
        level = std::move(next);
        pos >>= 1;
        if (cnt == 1) break;
    }
    return proof;
}

// ---------------------------------------------------------------------------------------------
// P2Pool merge mining slot and parameters

uint32_t AuxSlot(const uint256& chain_id, uint32_t nonce, uint32_t n_aux_chains)
{
    if (n_aux_chains <= 1) return 0;
    unsigned char buf[32 + 4 + 1];
    std::memcpy(buf, chain_id.begin(), 32);
    for (int i = 0; i < 4; ++i) buf[32 + i] = static_cast<unsigned char>(nonce >> (8 * i));
    buf[36] = 'm';
    unsigned char out[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(buf, sizeof(buf)).Finalize(out);
    const uint32_t v{uint32_t(out[0]) | (uint32_t(out[1]) << 8) | (uint32_t(out[2]) << 16) | (uint32_t(out[3]) << 24)};
    return v % n_aux_chains;
}

uint64_t EncodeMergeMiningParams(uint32_t n_aux_chains, uint32_t nonce)
{
    uint32_t n_bits{1};
    while ((1U << n_bits) < n_aux_chains && n_bits < 8) ++n_bits;
    return (n_bits - 1) | (uint64_t(n_aux_chains - 1) << 3) | (uint64_t(nonce) << (3 + n_bits));
}

bool DecodeMergeMiningParams(uint64_t value, uint32_t& n_aux_chains, uint32_t& nonce)
{
    const uint32_t n_bits{1 + uint32_t(value & 7)};
    n_aux_chains = 1 + uint32_t((value >> 3) & ((1U << n_bits) - 1));
    nonce = uint32_t(value >> (3 + n_bits));
    // Canonical only (no reserved bits set, minimal bit width): re-encoding must give the same value.
    return n_aux_chains >= 1 && n_aux_chains <= 256 && EncodeMergeMiningParams(n_aux_chains, nonce) == value;
}

// ---------------------------------------------------------------------------------------------
// Monero serialization helpers

void WriteMoneroVarInt(std::vector<unsigned char>& out, uint64_t v)
{
    while (v >= 0x80) {
        out.push_back(static_cast<unsigned char>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<unsigned char>(v));
}

bool ReadMoneroVarInt(std::span<const unsigned char>& in, uint64_t& v)
{
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (in.empty()) return false;
        const unsigned char b{in[0]};
        in = in.subspan(1);
        if (shift == 63 && (b & 0x7e)) return false; // overflow
        v |= uint64_t(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            if (b == 0 && shift > 0) return false; // non-canonical (trailing zero byte)
            return true;
        }
    }
    return false;
}

namespace {
bool Take(std::span<const unsigned char>& in, size_t n, std::span<const unsigned char>* out = nullptr)
{
    if (in.size() < n) return false;
    if (out) *out = in.first(n);
    in = in.subspan(n);
    return true;
}

constexpr unsigned char TX_EXTRA_PADDING{0x00};
constexpr unsigned char TX_EXTRA_PUBKEY{0x01};
constexpr unsigned char TX_EXTRA_NONCE{0x02};
constexpr unsigned char TX_EXTRA_MERGE_MINING{0x03};
constexpr unsigned char TX_EXTRA_ADDITIONAL_PUBKEYS{0x04};
} // namespace

bool ParseCoinbaseExtra(std::span<const unsigned char> extra, MergeMiningTag& tag, std::string& error)
{
    bool found{false};
    while (!extra.empty()) {
        const unsigned char type{extra[0]};
        extra = extra.subspan(1);
        switch (type) {
        case TX_EXTRA_PADDING:
            // Padding runs to the end and is all zeros.
            if (extra.size() > 254 || std::any_of(extra.begin(), extra.end(), [](unsigned char c) { return c != 0; })) {
                error = "bad tx_extra padding";
                return false;
            }
            extra = {};
            break;
        case TX_EXTRA_PUBKEY:
            if (!Take(extra, 32)) { error = "truncated tx_extra public key"; return false; }
            break;
        case TX_EXTRA_NONCE: {
            if (extra.empty()) { error = "truncated tx_extra nonce"; return false; }
            const size_t len{extra[0]};
            extra = extra.subspan(1);
            if (len > 255 || !Take(extra, len)) { error = "truncated tx_extra nonce"; return false; }
            break;
        }
        case TX_EXTRA_MERGE_MINING: {
            if (found) { error = "more than one merge mining tag"; return false; }
            uint64_t size;
            if (!ReadMoneroVarInt(extra, size) || size > extra.size()) { error = "truncated merge mining tag"; return false; }
            std::span<const unsigned char> body{extra.first(size)};
            extra = extra.subspan(size);
            uint64_t params;
            if (!ReadMoneroVarInt(body, params) || body.size() != 32) { error = "malformed merge mining tag"; return false; }
            if (!DecodeMergeMiningParams(params, tag.n_aux_chains, tag.nonce)) { error = "non-canonical merge mining parameters"; return false; }
            std::memcpy(tag.aux_root.begin(), body.data(), 32);
            found = true;
            break;
        }
        case TX_EXTRA_ADDITIONAL_PUBKEYS: {
            uint64_t n;
            if (!ReadMoneroVarInt(extra, n) || n > 1024 || !Take(extra, n * 32)) { error = "truncated additional public keys"; return false; }
            break;
        }
        default:
            error = strprintf("unsupported tx_extra field 0x%02x", type);
            return false;
        }
    }
    if (!found) error = "no merge mining tag in the Monero coinbase";
    return found;
}

// ---------------------------------------------------------------------------------------------
// CAuxPow

uint256 CAuxPow::CoinbaseHash() const
{
    CKeccak256 prefix{coinbase_midstate, head_size};
    std::vector<unsigned char> extra_len;
    WriteMoneroVarInt(extra_len, coinbase_extra.size());
    uint256 prefix_hash;
    prefix.Write(coinbase_tail).Write(extra_len).Write(coinbase_extra).Finalize(std::span<unsigned char, 32>{prefix_hash.begin(), 32});
    // Monero v2 transaction hash: H(prefix hash || H(RingCT base) || prunable hash). A coinbase has
    // RingCT type null: the base serialises as the single byte 0x00 and the prunable hash is null.
    static const uint256 RCT_NULL_BASE_HASH{Keccak256(std::array<unsigned char, 1>{0x00})};
    unsigned char buf[96];
    std::memcpy(buf, prefix_hash.begin(), 32);
    std::memcpy(buf + 32, RCT_NULL_BASE_HASH.begin(), 32);
    std::memset(buf + 64, 0, 32);
    return Keccak256(buf);
}

std::vector<unsigned char> CAuxPow::HashingBlob() const
{
    uint256 root;
    if (!MoneroRootFromProof(CoinbaseHash(), coinbase_branch, 0, tx_count, root)) return {};
    std::vector<unsigned char> blob{parent_header};
    blob.insert(blob.end(), root.begin(), root.end());
    WriteMoneroVarInt(blob, tx_count);
    return blob;
}

bool CheckAuxPowHeader(const CAuxPow& a, const uint256& block_hash, const uint256& chain_id,
                       std::vector<unsigned char>& blob, std::string& error)
{
    // Cheap bounds first.
    if (a.parent_header.empty() || a.parent_header.size() > MAX_PARENT_HEADER_SIZE) { error = "bad parent header size"; return false; }
    if (a.tx_count < 1 || a.tx_count > MAX_PARENT_TX_COUNT) { error = "bad parent transaction count"; return false; }
    if (a.coinbase_branch.size() > MAX_COINBASE_BRANCH) { error = "coinbase branch too long"; return false; }
    if (a.head_size % KECCAK_RATE != 0 || a.head_size > MAX_COINBASE_HEAD_SIZE) { error = "bad coinbase head size"; return false; }
    if (a.coinbase_tail.size() >= KECCAK_RATE) { error = "coinbase tail too long"; return false; }
    if (a.coinbase_extra.size() > MAX_COINBASE_EXTRA_SIZE) { error = "coinbase extra too long"; return false; }
    if (a.aux_branch.size() > MAX_AUX_BRANCH) { error = "aux branch too long"; return false; }
    if (a.head_size == 0 && a.coinbase_midstate != CKeccak256::State{}) { error = "midstate without head"; return false; }

    // The merge mining tag commits to the PQCH block id at PQCH's slot.
    MergeMiningTag tag;
    if (!ParseCoinbaseExtra(a.coinbase_extra, tag, error)) return false;
    const uint32_t slot{AuxSlot(chain_id, tag.nonce, tag.n_aux_chains)};
    uint256 aux_root;
    if (!MoneroRootFromProof(block_hash, a.aux_branch, slot, tag.n_aux_chains, aux_root)) { error = "bad aux branch length"; return false; }
    if (aux_root != tag.aux_root) { error = "aux branch does not lead to the merge mining root"; return false; }

    // The coinbase is the first transaction of the Monero block.
    blob = a.HashingBlob();
    if (blob.empty()) { error = "bad coinbase branch length"; return false; }
    return true;
}

bool CheckAuxPowCoinbase(const CAuxPow& a, const CAuxPowCoinbaseHead& h, std::string& error)
{
    if (h.head.size() != a.head_size) { error = "coinbase head size mismatch"; return false; }
    CKeccak256 k;
    k.Write(h.head);
    if (!k.AtBlockBoundary() || k.GetState() != a.coinbase_midstate) { error = "coinbase head does not match the midstate"; return false; }

    // head || tail must be exactly a canonical Monero v2 coinbase prefix up to tx_extra.
    std::vector<unsigned char> prefix{h.head};
    prefix.insert(prefix.end(), a.coinbase_tail.begin(), a.coinbase_tail.end());
    std::span<const unsigned char> in{prefix};
    uint64_t version, unlock_time, vin_count, height, vout_count;
    if (!ReadMoneroVarInt(in, version) || version != 2) { error = "coinbase is not a version 2 transaction"; return false; }
    if (!ReadMoneroVarInt(in, unlock_time)) { error = "bad coinbase unlock time"; return false; }
    if (!ReadMoneroVarInt(in, vin_count) || vin_count != 1) { error = "coinbase must have one input"; return false; }
    if (in.empty() || in[0] != 0xff) { error = "coinbase input is not a gen input"; return false; }
    in = in.subspan(1);
    if (!ReadMoneroVarInt(in, height)) { error = "bad coinbase height"; return false; }
    if (!ReadMoneroVarInt(in, vout_count) || vout_count > MAX_COINBASE_OUTPUTS) { error = "bad coinbase output count"; return false; }
    for (uint64_t i = 0; i < vout_count; ++i) {
        uint64_t amount;
        if (!ReadMoneroVarInt(in, amount) || in.empty()) { error = "bad coinbase output"; return false; }
        const unsigned char tag{in[0]};
        in = in.subspan(1);
        if (tag == 0x02) { // txout_to_key
            if (!Take(in, 32)) { error = "bad coinbase output key"; return false; }
        } else if (tag == 0x03) { // txout_to_tagged_key
            if (!Take(in, 33)) { error = "bad coinbase output key"; return false; }
        } else {
            error = "unsupported coinbase output type";
            return false;
        }
    }
    if (!in.empty()) { error = "coinbase prefix does not end at tx_extra"; return false; }
    return true;
}

bool BuildFromMoneroBlock(std::span<const unsigned char> block, const uint256& seed_hash,
                          const std::vector<uint256>& aux_branch, CAuxPow& a, CAuxPowCoinbaseHead& h, std::string& error)
{
    std::span<const unsigned char> in{block};
    const auto start{in.data()};
    uint64_t v;
    // Header: major version, minor version, timestamp, previous id, nonce.
    if (!ReadMoneroVarInt(in, v) || !ReadMoneroVarInt(in, v) || !ReadMoneroVarInt(in, v) || !Take(in, 32 + 4)) {
        error = "bad Monero block header";
        return false;
    }
    a.parent_header.assign(start, in.data());
    // Miner transaction prefix, up to tx_extra.
    const auto tx_start{in.data()};
    uint64_t vin_count, vout_count;
    if (!ReadMoneroVarInt(in, v) || v != 2 || !ReadMoneroVarInt(in, v) || !ReadMoneroVarInt(in, vin_count) || vin_count != 1 ||
        in.empty() || in[0] != 0xff) {
        error = "unsupported Monero miner transaction";
        return false;
    }
    in = in.subspan(1);
    if (!ReadMoneroVarInt(in, v) || !ReadMoneroVarInt(in, vout_count) || vout_count > MAX_COINBASE_OUTPUTS) {
        error = "bad Monero miner transaction";
        return false;
    }
    for (uint64_t i = 0; i < vout_count; ++i) {
        if (!ReadMoneroVarInt(in, v) || in.empty()) { error = "bad Monero output"; return false; }
        const unsigned char tag{in[0]};
        in = in.subspan(1);
        if (!Take(in, tag == 0x02 ? 32 : tag == 0x03 ? 33 : SIZE_MAX)) { error = "bad Monero output"; return false; }
    }
    const std::span<const unsigned char> prefix{tx_start, size_t(in.data() - tx_start)};
    uint64_t extra_len;
    std::span<const unsigned char> extra;
    if (!ReadMoneroVarInt(in, extra_len) || extra_len > MAX_COINBASE_EXTRA_SIZE || !Take(in, extra_len, &extra)) {
        error = "bad Monero tx_extra";
        return false;
    }
    if (in.empty() || in[0] != 0x00) { error = "Monero miner transaction RingCT type is not null"; return false; }
    in = in.subspan(1);
    // Transaction hashes.
    uint64_t n_hashes;
    if (!ReadMoneroVarInt(in, n_hashes) || n_hashes >= MAX_PARENT_TX_COUNT || in.size() != n_hashes * 32) {
        error = "bad Monero transaction list";
        return false;
    }
    a.seed_hash = seed_hash;
    a.tx_count = n_hashes + 1;
    a.head_size = (prefix.size() / KECCAK_RATE) * KECCAK_RATE;
    h.head.assign(prefix.begin(), prefix.begin() + a.head_size);
    CKeccak256 k;
    k.Write(h.head);
    a.coinbase_midstate = a.head_size ? k.GetState() : CKeccak256::State{};
    a.coinbase_tail.assign(prefix.begin() + a.head_size, prefix.end());
    a.coinbase_extra.assign(extra.begin(), extra.end());
    std::vector<uint256> tx_hashes{a.CoinbaseHash()};
    for (uint64_t i = 0; i < n_hashes; ++i) {
        uint256 hh;
        std::memcpy(hh.begin(), in.data() + 32 * i, 32);
        tx_hashes.push_back(hh);
    }
    a.coinbase_branch = MoneroProof(tx_hashes, 0);
    a.aux_branch = aux_branch;
    return true;
}

} // namespace auxpow
