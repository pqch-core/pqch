/* PQCH: constant-time check of slhdsa-c (SLH-DSA-SHA2-128s) with valgrind (ctgrind method).
// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying file COPYING or
// https://opensource.org/license/mit/.
 * Secret bytes are marked "undefined"; memcheck then reports every branch or memory address that
 * depends on them. Mode: keygen | sign_seed | sign_prf | sign_all. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <valgrind/memcheck.h>
#include "slh_dsa.h"
int main(int argc, char **argv) {
  const slh_param_t *p = &slh_dsa_sha2_128s;
  uint8_t sk[64], pk[32], seed[16], prf[16], pks[16], sig[7856], rnd[16], msg[32];
  for (int i = 0; i < 16; i++) { seed[i] = i * 7 + 1; prf[i] = i * 13 + 5; pks[i] = i * 3 + 9; rnd[i] = 200 - i; }
  memset(msg, 0x42, sizeof msg);
  const char *mode = argc > 1 ? argv[1] : "keygen";
  if (!strcmp(mode, "keygen")) {
    VALGRIND_MAKE_MEM_UNDEFINED(seed, 16);
    VALGRIND_MAKE_MEM_UNDEFINED(prf, 16);
    slh_keygen_internal(sk, pk, seed, prf, pks, p);
    VALGRIND_MAKE_MEM_DEFINED(pk, 32); /* the public key is published */
    printf("keygen done %02x\n", pk[0]);
    return 0;
  }
  slh_keygen_internal(sk, pk, seed, prf, pks, p);
  if (!strcmp(mode, "sign_seed")) VALGRIND_MAKE_MEM_UNDEFINED(sk, 16);          /* SK.seed */
  if (!strcmp(mode, "sign_all")) { VALGRIND_MAKE_MEM_UNDEFINED(sk, 32); VALGRIND_MAKE_MEM_UNDEFINED(rnd, 16); }
  if (!strcmp(mode, "sign_prf")) { VALGRIND_MAKE_MEM_UNDEFINED(sk + 16, 16); VALGRIND_MAKE_MEM_UNDEFINED(rnd, 16); }
  size_t n = slh_sign(sig, msg, sizeof msg, (const uint8_t *)"PQCH/tx/v1", 10, sk, rnd, p);
  VALGRIND_MAKE_MEM_DEFINED(sig, sizeof sig);
  printf("sign %s: %zu bytes, verify %d\n", mode, n, slh_verify(msg, sizeof msg, sig, n, (const uint8_t *)"PQCH/tx/v1", 10, pk, p));
  return 0;
}
