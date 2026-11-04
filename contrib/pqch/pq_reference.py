#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
PQCH: differential test vectors for the post-quantum signatures, made with implementations that
are independent from the ones in the node (whitepaper, section 21.9):

  ML-DSA-44         dilithium-py  (pip install dilithium-py==1.5.1)
  SLH-DSA-SHA2-128s slh-dsa       (pip install slh-dsa==0.2.5)

For each seed, the vectors fix the public key and the deterministic signature of a 32-byte
message under the PQCH context string "PQCH/tx/v1". The node must produce exactly the same bytes
(src/test/pqch_pq_tests.cpp), which shows that key derivation, message formatting and signing
agree with the standard; verification of the other implementation's signatures follows.

Usage: python3 pq_reference.py > vectors.txt
"""
import hashlib
import time

from dilithium_py.ml_dsa import ML_DSA_44
from slhdsa.lowlevel import slhdsa as slh_low
from slhdsa.lowlevel.addresses import Address
from slhdsa.lowlevel.parameters import sha2_128s
from slhdsa.lowlevel.xmss import XMSS

CTX = b"PQCH/tx/v1"


def h(b):
    return hashlib.sha256(b).hexdigest()


def mldsa_vector(tag):
    seed = bytes([tag]) * 32
    msg = hashlib.sha256(b"PQCH test message %d" % tag).digest()
    pk, sk = ML_DSA_44.key_derive(seed)
    sig = ML_DSA_44.sign(sk, msg, ctx=CTX, deterministic=True)
    assert ML_DSA_44.verify(pk, msg, sig, ctx=CTX)
    return seed.hex(), msg.hex(), h(pk), h(sig)


def slhdsa_vector(tag):
    seed = bytes([tag]) * 32
    msg = hashlib.sha256(b"PQCH test message %d" % tag).digest()
    par = sha2_128s
    expanded = hashlib.sha512(b"PQCH/slh-dsa/keygen" + seed).digest()
    sk_seed, sk_prf, pk_seed = expanded[0:16], expanded[16:32], expanded[32:48]
    pk_root = XMSS(par).node(sk_seed, 0, par.h_m, pk_seed, Address(par.d - 1, 0))
    m_prime = b"\x00" + bytes([len(CTX)]) + CTX + msg  # FIPS 205 pure signing
    sig = slh_low.sign(m_prime, (sk_seed, sk_prf, pk_seed, pk_root), par, randomize=False)
    assert slh_low.verify(m_prime, sig, (pk_seed, pk_root), par)
    return seed.hex(), msg.hex(), h(pk_seed + pk_root), h(sig)


if __name__ == "__main__":
    for tag in (1, 2, 0x5a):
        print("MLDSA44", *mldsa_vector(tag))
    for tag in (1, 2):
        t = time.time()
        print("SLHDSA128S", *slhdsa_vector(tag))
        print("# SLH-DSA vector in %.0f s" % (time.time() - t))
