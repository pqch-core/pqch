// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_PQC_PQC_H
#define BITCOIN_CRYPTO_PQC_PQC_H

#include <support/cleanse.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/**
 * PQCH post-quantum signatures (whitepaper, section 21).
 *
 *  - ML-DSA-44 (NIST FIPS 204): main signature. Implementation: mldsa-native v2.0.0.
 *  - SLH-DSA-SHA2-128s (NIST FIPS 205): hash-based recovery signature. Implementation: slhdsa-c.
 *
 * Both are used in their "pure" form with a FIPS context string, so a signature made for PQCH
 * transactions can never be valid in another protocol that uses the same keys.
 *
 * Only verification is consensus critical. It is deterministic and rejects any key or signature
 * whose length is not exactly the standard one.
 */
namespace pqc {

inline constexpr size_t MLDSA44_PUBKEY_SIZE{1312};
inline constexpr size_t MLDSA44_SECKEY_SIZE{2560};
inline constexpr size_t MLDSA44_SIG_SIZE{2420};

inline constexpr size_t SLHDSA128S_PUBKEY_SIZE{32};
inline constexpr size_t SLHDSA128S_SECKEY_SIZE{64};
inline constexpr size_t SLHDSA128S_SIG_SIZE{7856};

/** Size of the seed from which a key pair is derived deterministically. */
inline constexpr size_t KEY_SEED_SIZE{32};

/** FIPS 204/205 context string for PQCH transaction signatures. */
inline constexpr std::array<unsigned char, 10> TX_SIGNATURE_CONTEXT{'P', 'Q', 'C', 'H', '/', 't', 'x', '/', 'v', '1'};

/** ML-DSA-44 verification (FIPS 204, Algorithm 3). False for any malformed input. */
[[nodiscard]] bool MLDSA44Verify(std::span<const unsigned char> sig, std::span<const unsigned char> msg,
                                 std::span<const unsigned char> ctx, std::span<const unsigned char> pubkey);

/** SLH-DSA-SHA2-128s verification (FIPS 205, Algorithm 24). False for any malformed input. */
[[nodiscard]] bool SLHDSA128sVerify(std::span<const unsigned char> sig, std::span<const unsigned char> msg,
                                    std::span<const unsigned char> ctx, std::span<const unsigned char> pubkey);

/** Key pair (wallet side; not consensus critical). The secret key is wiped when it goes away. */
struct KeyPair {
    std::vector<unsigned char> pubkey;
    std::vector<unsigned char> seckey;

    KeyPair() = default;
    KeyPair(const KeyPair&) = default;
    KeyPair(KeyPair&&) = default;
    KeyPair& operator=(const KeyPair&) = default;
    KeyPair& operator=(KeyPair&& other)
    {
        WipeSecret();
        pubkey = std::move(other.pubkey);
        seckey = std::move(other.seckey);
        return *this;
    }
    ~KeyPair() { WipeSecret(); }

private:
    //! An empty vector may have a null data(): memset on it is undefined even with length 0.
    void WipeSecret() { if (!seckey.empty()) memory_cleanse(seckey.data(), seckey.size()); }
};

/** ML-DSA-44 key pair from a 32-byte seed (FIPS 204, Algorithm 6, ML-DSA.KeyGen_internal). */
[[nodiscard]] bool MLDSA44KeyFromSeed(std::span<const unsigned char> seed, KeyPair& out);

/**
 * SLH-DSA-SHA2-128s key pair from a 32-byte seed. The FIPS 205 inputs SK.seed, SK.prf and PK.seed
 * (16 bytes each) are the first 48 bytes of SHA-512("PQCH/slh-dsa/keygen" || seed).
 */
[[nodiscard]] bool SLHDSA128sKeyFromSeed(std::span<const unsigned char> seed, KeyPair& out);

/**
 * ML-DSA-44 signature. `rnd` is FIPS 204's 32-byte randomness: all zeros gives the deterministic
 * variant; wallets should pass fresh random bytes (the hedged variant, the FIPS 204 default).
 */
[[nodiscard]] bool MLDSA44Sign(std::span<const unsigned char> seckey, std::span<const unsigned char> msg,
                               std::span<const unsigned char> ctx, std::span<const unsigned char> rnd,
                               std::vector<unsigned char>& sig);

/** SLH-DSA-SHA2-128s signature. `addrnd` is empty (deterministic) or 16 random bytes (hedged). */
[[nodiscard]] bool SLHDSA128sSign(std::span<const unsigned char> seckey, std::span<const unsigned char> msg,
                                  std::span<const unsigned char> ctx, std::span<const unsigned char> addrnd,
                                  std::vector<unsigned char>& sig);

} // namespace pqc

#endif // BITCOIN_CRYPTO_PQC_PQC_H
