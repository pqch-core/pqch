#!/usr/bin/env bash
# PQCH: stack residue check for slhdsa-c (see slhdsa_stack.cpp). Needs a built tree (default: build).
# Usage: contrib/pqch/ct/slhdsa_stack.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/../../.."
B=${1:-build}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for o in -O0 -O2; do
  ${CXX:-g++} -std=c++20 $o -Isrc -I"$B/src" contrib/pqch/ct/slhdsa_stack.cpp "$B/lib/libbitcoin_crypto.a" "$B/lib/libpqch_pqc_c.a" -o "$T/stack"
  "$T/stack" --raw
  "$T/stack"
done
echo "OK: no copy of SK.seed left on the stack by the wrapper"
