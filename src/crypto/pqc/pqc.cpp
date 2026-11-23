// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/pqc/pqc.h>

#include <crypto/sha512.h>
#include <support/cleanse.h>

// Compile definitions (parameter set 44, namespace PQCH_mldsa44, no randomized API) are set by
// the build for both the library and this file; see src/crypto/CMakeLists.txt.
#include <crypto/pqc/mldsa-native/mldsa_native.h>
#include <crypto/pqc/slhdsa-c/slh_dsa.h>

#include <cstring>

static_assert(MLDSA_PUBLICKEYBYTES(44) == pqc::MLDSA44_PUBKEY_SIZE);
static_assert(MLDSA_SECRETKEYBYTES(44) == pqc::MLDSA44_SECKEY_SIZE);
static_assert(MLDSA_BYTES(44) == pqc::MLDSA44_SIG_SIZE);
static_assert(MLDSA_SEEDBYTES == pqc::KEY_SEED_SIZE);

namespace pqc {

namespace {
// FIPS 204 and 205 limit context strings to 255 bytes.
constexpr size_t MAX_CONTEXT_SIZE{255};
constexpr size_t SLH_N{16};

/**
 * slhdsa-c does not clear its stack: after keygen or signing, copies of SK.seed and SK.prf
 * (slh_var_t) and intermediate WOTS+/FORS secret values stay in the stack frames it used (about
 * 5 KiB with gcc and clang). The library is kept unchanged (PQCH-VERSION.md), so right after each
 * call that handles an SLH-DSA secret key this function, called from the same frame, overwrites the
 * stack area below it. 64 KiB leaves a wide margin; checked by contrib/pqch/ct/slhdsa_stack.sh.
 */
constexpr size_t SLH_STACK_CLEAR_SIZE{64 * 1024};
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void ClearSLHDSAStack()
{
    unsigned char area[SLH_STACK_CLEAR_SIZE];
    memory_cleanse(area, sizeof(area));
}
} // namespace

bool MLDSA44Verify(std::span<const unsigned char> sig, std::span<const unsigned char> msg,
                   std::span<const unsigned char> ctx, std::span<const unsigned char> pubkey)
{
    // Strict encoding: exact lengths only (the library also rejects non-canonical hints).
    if (sig.size() != MLDSA44_SIG_SIZE || pubkey.size() != MLDSA44_PUBKEY_SIZE || ctx.size() > MAX_CONTEXT_SIZE) return false;
    return PQCH_mldsa44_verify(sig.data(), msg.data(), msg.size(), ctx.data(), ctx.size(), pubkey.data()) == 0;
}

bool SLHDSA128sVerify(std::span<const unsigned char> sig, std::span<const unsigned char> msg,
                      std::span<const unsigned char> ctx, std::span<const unsigned char> pubkey)
{
    if (sig.size() != SLHDSA128S_SIG_SIZE || pubkey.size() != SLHDSA128S_PUBKEY_SIZE || ctx.size() > MAX_CONTEXT_SIZE) return false;
    return slh_verify(msg.data(), msg.size(), sig.data(), sig.size(), ctx.data(), ctx.size(), pubkey.data(), &slh_dsa_sha2_128s) == 1;
}

bool MLDSA44KeyFromSeed(std::span<const unsigned char> seed, KeyPair& out)
{
    if (seed.size() != KEY_SEED_SIZE) return false;
    out.pubkey.assign(MLDSA44_PUBKEY_SIZE, 0);
    out.seckey.assign(MLDSA44_SECKEY_SIZE, 0);
    return PQCH_mldsa44_keypair_internal(out.pubkey.data(), out.seckey.data(), seed.data()) == 0;
}

bool SLHDSA128sKeyFromSeed(std::span<const unsigned char> seed, KeyPair& out)
{
    if (seed.size() != KEY_SEED_SIZE) return false;
    static constexpr char TAG[]{"PQCH/slh-dsa/keygen"};
    unsigned char expanded[CSHA512::OUTPUT_SIZE];
    CSHA512().Write(reinterpret_cast<const unsigned char*>(TAG), sizeof(TAG) - 1).Write(seed.data(), seed.size()).Finalize(expanded);
    out.pubkey.assign(SLHDSA128S_PUBKEY_SIZE, 0);
    out.seckey.assign(SLHDSA128S_SECKEY_SIZE, 0);
    const int ret{slh_keygen_internal(out.seckey.data(), out.pubkey.data(), expanded, expanded + SLH_N, expanded + 2 * SLH_N, &slh_dsa_sha2_128s)};
    memory_cleanse(expanded, sizeof(expanded));
    ClearSLHDSAStack();
    return ret == 0;
}

bool MLDSA44Sign(std::span<const unsigned char> seckey, std::span<const unsigned char> msg,
                 std::span<const unsigned char> ctx, std::span<const unsigned char> rnd,
                 std::vector<unsigned char>& sig)
{
    if (seckey.size() != MLDSA44_SECKEY_SIZE || rnd.size() != MLDSA_RNDBYTES || ctx.size() > MAX_CONTEXT_SIZE) return false;
    // Pure ML-DSA: M' = 0x00 || len(ctx) || ctx || M (FIPS 204, Algorithm 2).
    std::vector<unsigned char> prefix{0x00, static_cast<unsigned char>(ctx.size())};
    prefix.insert(prefix.end(), ctx.begin(), ctx.end());
    sig.assign(MLDSA44_SIG_SIZE, 0);
    return PQCH_mldsa44_signature_internal(sig.data(), msg.data(), msg.size(), prefix.data(), prefix.size(),
                                           rnd.data(), seckey.data(), /*externalmu=*/0) == 0;
}

bool SLHDSA128sSign(std::span<const unsigned char> seckey, std::span<const unsigned char> msg,
                    std::span<const unsigned char> ctx, std::span<const unsigned char> addrnd,
                    std::vector<unsigned char>& sig)
{
    if (seckey.size() != SLHDSA128S_SECKEY_SIZE || ctx.size() > MAX_CONTEXT_SIZE) return false;
    if (!addrnd.empty() && addrnd.size() != SLH_N) return false;
    sig.assign(SLHDSA128S_SIG_SIZE, 0);
    const size_t len{slh_sign(sig.data(), msg.data(), msg.size(), ctx.data(), ctx.size(), seckey.data(),
                              addrnd.empty() ? nullptr : addrnd.data(), &slh_dsa_sha2_128s)};
    ClearSLHDSAStack();
    return len == SLHDSA128S_SIG_SIZE;
}

} // namespace pqc
