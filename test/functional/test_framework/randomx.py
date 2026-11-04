#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: RandomX v2 proof of work for blocks built by the Python test framework.

The node checks RandomX_v2(key = seed block hash, input = 80-byte header) <= target. This module
calls the RandomX library through build/lib/libpqch_randomx_py.so (built with the tests).

Seed schedule (src/pow_randomx.h): the key is the genesis block hash up to height
interval + lag = 4096 + 64 = 4160 on regtest, which covers the functional tests. Above that the
framework would need the chain's block hashes, so it refuses.
"""
import ctypes
import os

RANDOMX_SEED_INTERVAL = 4096
RANDOMX_SEED_LAG = 64
# PQCH regtest genesis block (src/kernel/chainparams.cpp).
REGTEST_GENESIS_HASH = bytes.fromhex("dc23bf8241b903f340911cf5199292b79b6ca42078d45c1dca9ccfe18678364b")[::-1]

_lib = None


def _load():
    global _lib
    if _lib is None:
        path = os.environ.get("PQCH_RANDOMX_LIB")
        if not path:
            here = os.path.dirname(os.path.abspath(__file__))
            path = os.path.join(here, "..", "..", "..", "build", "lib", "libpqch_randomx_py.so")
        _lib = ctypes.CDLL(path)
        _lib.pqch_randomx_hash.restype = ctypes.c_int
        _lib.pqch_randomx_hash.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    return _lib


def seed_for_height(height):
    """RandomX key (internal byte order) for a block at `height` on regtest."""
    if height is None or height <= RANDOMX_SEED_INTERVAL + RANDOMX_SEED_LAG:
        return REGTEST_GENESIS_HASH
    raise NotImplementedError("PQCH test framework: RandomX key above height 4160 needs the chain's block hashes")


def randomx_hash(key, data):
    """RandomX v2 hash of `data` under `key`, as an integer (little-endian, like uint256)."""
    out = ctypes.create_string_buffer(32)
    assert _load().pqch_randomx_hash(key, len(key), data, len(data), out) == 1
    return int.from_bytes(out.raw, "little")
