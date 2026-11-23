// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying file COPYING or
// https://opensource.org/license/mit/.
// PQCH: checks that no copy of the SLH-DSA secret seed is left on the stack after pqc::SLHDSA128sSign
// and pqc::SLHDSA128sKeyFromSeed (slhdsa-c does not clear its stack; pqc.cpp does it for it).
// With "--raw" it calls slh_sign directly, to show the copy the library leaves behind.
#include <crypto/pqc/pqc.h>
#include <crypto/pqc/slhdsa-c/slh_dsa.h>
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr size_t PROBE{256 * 1024};
static unsigned char g_seed[16];

__attribute__((noinline)) static int CountSeedCopies()
{
    volatile unsigned char area[PROBE];
    int hits{0};
    for (size_t i = 0; i + 16 <= PROBE; ++i) {
        size_t k{0};
        while (k < 16 && area[i + k] == g_seed[k]) ++k;
        if (k == 16) ++hits;
    }
    return hits;
}

int main(int argc, char** argv)
{
    const bool raw{argc > 1 && !strcmp(argv[1], "--raw")};
    std::vector<unsigned char> seed(32, 0x77), msg(32, 0x42), ctx{'P', 'Q', 'C', 'H'}, sig;
    pqc::KeyPair kp;
    if (!pqc::SLHDSA128sKeyFromSeed(seed, kp)) return 2;
    memcpy(g_seed, kp.seckey.data(), 16); // SK.seed: the first 16 bytes of the secret key
    const int after_keygen{CountSeedCopies()};
    if (raw) {
        sig.assign(pqc::SLHDSA128S_SIG_SIZE, 0);
        slh_sign(sig.data(), msg.data(), msg.size(), ctx.data(), ctx.size(), kp.seckey.data(), nullptr, &slh_dsa_sha2_128s);
    } else if (!pqc::SLHDSA128sSign(kp.seckey, msg, ctx, {}, sig)) {
        return 2;
    }
    const int after_sign{CountSeedCopies()};
    printf("%s: SK.seed copies on the stack after keygen %d, after signing %d\n", raw ? "slh_sign directly" : "pqc wrapper", after_keygen, after_sign);
    return raw ? 0 : (after_keygen || after_sign) ? 1 : 0;
}
