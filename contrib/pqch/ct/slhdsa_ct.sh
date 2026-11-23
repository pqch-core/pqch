#!/usr/bin/env bash
# PQCH: constant-time check of slhdsa-c (SLH-DSA-SHA2-128s) with valgrind (ctgrind method).
# Uses the library's own SLH_CONFIG_CT_TESTING_ENABLED mode, which declassifies only values that are
# public by design (R, the FORS public key, the XMSS roots, the public key). Any memcheck error means
# a branch or a memory address depends on a secret. Needs gcc or clang, valgrind and its headers.
# Usage: contrib/pqch/ct/slhdsa_ct.sh [compiler...]
set -euo pipefail
cd "$(dirname "$0")"
S=../../../src/crypto/pqc/slhdsa-c
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
falhas=0
for cc in ${@:-gcc clang}; do
  for o in -O0 -O2 -O3 -Os; do
    command -v "$cc" >/dev/null || { echo "$cc: not found"; continue; }
    "$cc" $o -g -DSLH_CONFIG_CT_TESTING_ENABLED -I"$S" slhdsa_ct.c "$S"/*.c -o "$T/ct"
    for m in keygen sign_seed sign_prf sign_all; do
      r=$(valgrind --error-exitcode=99 "$T/ct" "$m" 2>&1) && st=0 || st=$?
      n=$(grep -o 'ERROR SUMMARY: [0-9]*' <<<"$r" | grep -o '[0-9]*$')
      printf '%-6s %-4s %-10s %s errors\n' "$cc" "$o" "$m" "$n"
      if [ "$st" -ne 0 ] || [ "$n" != 0 ] || ! grep -qE '^(keygen done|sign .*verify 1)' <<<"$r"; then falhas=1; fi
    done
  done
done
[ "$falhas" = 0 ] && echo "OK: no secret-dependent branches or addresses" || { echo "FAILED"; exit 1; }
