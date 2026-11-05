// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_BIP39_H
#define BITCOIN_SCRIPT_BIP39_H

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/**
 * PQCH: BIP39 recovery words (whitepaper, section 21.5; decision of 5 Nov 2026).
 *
 * Wallets use 24 English words (256 bits of entropy, 8-bit checksum) and an optional passphrase.
 * The BIP39 seed is the standard PBKDF2-HMAC-SHA512(words, "mnemonic" || passphrase, 2048), so the
 * words work with BIP39 tools and backup cards; the wallet's post-quantum seed comes from it
 * (p2mr::SeedFromMnemonic). Everything here is hash based, so it keeps 128-bit security against a
 * quantum computer (Grover).
 *
 * Unicode normalisation (NFKD) is not implemented: the English words are ASCII, and passphrases
 * must be printable ASCII, so that a passphrase can never mean different bytes in another wallet.
 */
namespace bip39 {

inline constexpr size_t ENTROPY_SIZE{32};
inline constexpr size_t WORD_COUNT{24};
inline constexpr size_t SEED_SIZE{64};

/** The 24 words for 32 bytes of entropy, separated by single spaces. */
std::string MnemonicFromEntropy(std::span<const unsigned char, ENTROPY_SIZE> entropy);

/**
 * The entropy of 24 words, or nullopt if a word is not in the list or the checksum is wrong.
 * Words may be separated by any ASCII whitespace; letters must be lowercase.
 */
std::optional<std::array<unsigned char, ENTROPY_SIZE>> EntropyFromMnemonic(std::string_view mnemonic);

/** The words in canonical form (single spaces), or nullopt if they are not valid. */
std::optional<std::string> NormalizeMnemonic(std::string_view mnemonic);

/** Whether a passphrase is accepted (printable ASCII only, see above). */
bool IsValidPassphrase(std::string_view passphrase);

/** The BIP39 seed of valid words and passphrase; nullopt if either is invalid. */
std::optional<std::array<unsigned char, SEED_SIZE>> SeedFromMnemonic(std::string_view mnemonic, std::string_view passphrase);

} // namespace bip39

#endif // BITCOIN_SCRIPT_BIP39_H
