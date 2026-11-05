// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/bip39.h>

#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <script/bip39_english.h>
#include <support/cleanse.h>

#include <algorithm>
#include <vector>

namespace bip39 {
namespace {

std::vector<std::string_view> SplitWords(std::string_view s)
{
    std::vector<std::string_view> words;
    size_t i{0};
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        const size_t start{i};
        while (i < s.size() && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        if (i > start) words.push_back(s.substr(start, i - start));
    }
    return words;
}

std::optional<int> WordIndex(std::string_view word)
{
    // The list is sorted, so a binary search finds a word.
    const auto it{std::lower_bound(ENGLISH.begin(), ENGLISH.end(), word)};
    if (it == ENGLISH.end() || *it != word) return std::nullopt;
    return static_cast<int>(it - ENGLISH.begin());
}

} // namespace

std::string MnemonicFromEntropy(std::span<const unsigned char, ENTROPY_SIZE> entropy)
{
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(hash);
    // 256 bits of entropy followed by the first 8 bits of its SHA256: 264 bits, 24 groups of 11.
    std::array<unsigned char, ENTROPY_SIZE + 1> bits{};
    std::copy(entropy.begin(), entropy.end(), bits.begin());
    bits[ENTROPY_SIZE] = hash[0];
    std::string out;
    for (size_t w = 0; w < WORD_COUNT; ++w) {
        int index{0};
        for (size_t b = 0; b < 11; ++b) {
            const size_t bit{w * 11 + b};
            index = (index << 1) | ((bits[bit / 8] >> (7 - bit % 8)) & 1);
        }
        if (w) out += ' ';
        out += ENGLISH[index];
    }
    memory_cleanse(bits.data(), bits.size());
    return out;
}

std::optional<std::array<unsigned char, ENTROPY_SIZE>> EntropyFromMnemonic(std::string_view mnemonic)
{
    const auto words{SplitWords(mnemonic)};
    if (words.size() != WORD_COUNT) return std::nullopt;
    std::array<unsigned char, ENTROPY_SIZE + 1> bits{};
    for (size_t w = 0; w < WORD_COUNT; ++w) {
        const auto index{WordIndex(words[w])};
        if (!index) return std::nullopt;
        for (size_t b = 0; b < 11; ++b) {
            if ((*index >> (10 - b)) & 1) {
                const size_t bit{w * 11 + b};
                bits[bit / 8] |= static_cast<unsigned char>(1 << (7 - bit % 8));
            }
        }
    }
    std::array<unsigned char, ENTROPY_SIZE> entropy;
    std::copy(bits.begin(), bits.begin() + ENTROPY_SIZE, entropy.begin());
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(hash);
    const bool ok{hash[0] == bits[ENTROPY_SIZE]};
    memory_cleanse(bits.data(), bits.size());
    if (!ok) {
        memory_cleanse(entropy.data(), entropy.size());
        return std::nullopt;
    }
    return entropy;
}

std::optional<std::string> NormalizeMnemonic(std::string_view mnemonic)
{
    auto entropy{EntropyFromMnemonic(mnemonic)};
    if (!entropy) return std::nullopt;
    std::string out{MnemonicFromEntropy(*entropy)};
    memory_cleanse(entropy->data(), entropy->size());
    return out;
}

bool IsValidPassphrase(std::string_view passphrase)
{
    return std::all_of(passphrase.begin(), passphrase.end(), [](char c) { return c >= 0x20 && c <= 0x7e; });
}

std::optional<std::array<unsigned char, SEED_SIZE>> SeedFromMnemonic(std::string_view mnemonic, std::string_view passphrase)
{
    auto words{NormalizeMnemonic(mnemonic)};
    if (!words || !IsValidPassphrase(passphrase)) return std::nullopt;
    // PBKDF2-HMAC-SHA512 with one 64-byte block: U1 = HMAC(P, S || 00000001), Ui = HMAC(P, Ui-1).
    std::string salt{"mnemonic"};
    salt += passphrase;
    const auto prf{[&](const unsigned char* data, size_t len, unsigned char out[CHMAC_SHA512::OUTPUT_SIZE]) {
        CHMAC_SHA512{reinterpret_cast<const unsigned char*>(words->data()), words->size()}.Write(data, len).Finalize(out);
    }};
    std::vector<unsigned char> first{salt.begin(), salt.end()};
    first.insert(first.end(), {0, 0, 0, 1});
    unsigned char u[CHMAC_SHA512::OUTPUT_SIZE];
    prf(first.data(), first.size(), u);
    std::array<unsigned char, SEED_SIZE> seed;
    std::copy(u, u + SEED_SIZE, seed.begin());
    for (int i = 1; i < 2048; ++i) {
        unsigned char next[CHMAC_SHA512::OUTPUT_SIZE];
        prf(u, sizeof(u), next);
        std::copy(next, next + sizeof(next), u);
        for (size_t j = 0; j < SEED_SIZE; ++j) seed[j] ^= u[j];
        memory_cleanse(next, sizeof(next));
    }
    memory_cleanse(u, sizeof(u));
    memory_cleanse(first.data(), first.size());
    memory_cleanse(salt.data(), salt.size());
    memory_cleanse(words->data(), words->size());
    return seed;
}

} // namespace bip39
