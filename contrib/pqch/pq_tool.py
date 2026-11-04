#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
PQCH: minimal post-quantum signing tool for tests (phase 4), until the wallet supports P2MR.

It derives keys from a seed, builds P2MR addresses and signs transactions that spend them, exactly
as specified in the whitepaper (section 21) and implemented in the node:

  leaf version 0xc2 (PQ-script), signature hash = BIP341/342 with key_version = 1 (ML-DSA-44)
  or 2 (SLH-DSA-SHA2-128s), FIPS 204/205 context "PQCH/tx/v1".

Kinds of address (script tree):
  mldsa      one leaf:  <ML-DSA key> OP_CHECKSIGMLDSA
  hybrid     one leaf:  <Schnorr key> OP_CHECKSIGVERIFY <ML-DSA key> OP_CHECKSIGMLDSA   (wallet default)
  recovery   two leaves: A = hybrid, B = <SLH-DSA key> OP_CHECKSIGSLHDSA

Requires: pip install dilithium-py==1.5.1 slh-dsa==0.2.5 (signatures made by these independent
implementations, verified by the node: a differential test in itself).

Usage:
  pq_tool.py address --seed HEX --kind hybrid --hrp pqcrt
  pq_tool.py spend --seed HEX --kind hybrid --hrp pqcrt --txid TXID --vout N --amount QUANTA \
             --to ADDRESS --fee QUANTA [--leaf 0|1] [--corrupt]
"""
import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "functional"))

from dilithium_py.ml_dsa import ML_DSA_44  # noqa: E402
from slhdsa.lowlevel import slhdsa as slh_low  # noqa: E402
from slhdsa.lowlevel.addresses import Address  # noqa: E402
from slhdsa.lowlevel.parameters import sha2_128s  # noqa: E402
from slhdsa.lowlevel.xmss import XMSS  # noqa: E402

from test_framework.key import TaggedHash, compute_xonly_pubkey, sign_schnorr  # noqa: E402
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut  # noqa: E402
from test_framework.script import CScript, CScriptOp, OP_1, OP_CHECKSIGVERIFY, TaprootSignatureMsg  # noqa: E402
from test_framework.segwit_addr import decode_segwit_address, encode_segwit_address  # noqa: E402

CTX = b"PQCH/tx/v1"
LEAF_PQSCRIPT = 0xc2
OP_CHECKSIGMLDSA = CScriptOp(0xc0)
OP_CHECKSIGSLHDSA = CScriptOp(0xc2)
KEY_VERSION_MLDSA = 1
KEY_VERSION_SLHDSA = 2


def ser_string(b):
    n = len(b)
    if n < 253:
        return bytes([n]) + b
    if n <= 0xffff:
        return b"\xfd" + n.to_bytes(2, "little") + b
    return b"\xfe" + n.to_bytes(4, "little") + b


class Keys:
    def __init__(self, seed):
        self.mldsa_pk, self.mldsa_sk = ML_DSA_44.key_derive(seed)
        self.ec_sk = hashlib.sha256(b"PQCH/test-schnorr/" + seed).digest()
        self.ec_pk = compute_xonly_pubkey(self.ec_sk)[0]
        e = hashlib.sha512(b"PQCH/slh-dsa/keygen" + seed).digest()
        sk_seed, sk_prf, pk_seed = e[0:16], e[16:32], e[32:48]
        pk_root = XMSS(sha2_128s).node(sk_seed, 0, sha2_128s.h_m, pk_seed, Address(sha2_128s.d - 1, 0))
        self.slh_sk = (sk_seed, sk_prf, pk_seed, pk_root)
        self.slh_pk = pk_seed + pk_root


def leaf_mldsa(k):
    return CScript([k.mldsa_pk, OP_CHECKSIGMLDSA])


def leaf_hybrid(k):
    return CScript([k.ec_pk, OP_CHECKSIGVERIFY, k.mldsa_pk, OP_CHECKSIGMLDSA])


def leaf_slhdsa(k):
    return CScript([k.slh_pk, OP_CHECKSIGSLHDSA])


def leaves_for(kind, k):
    return {"mldsa": [leaf_mldsa(k)], "hybrid": [leaf_hybrid(k)], "recovery": [leaf_hybrid(k), leaf_slhdsa(k)]}[kind]


def leaf_hash(script):
    return TaggedHash("TapLeaf", bytes([LEAF_PQSCRIPT]) + ser_string(bytes(script)))


def branch(a, b):
    return TaggedHash("TapBranch", min(a, b) + max(a, b))


def root_and_controls(leaves):
    hashes = [leaf_hash(s) for s in leaves]
    if len(hashes) == 1:
        return hashes[0], [bytes([LEAF_PQSCRIPT | 1])]
    assert len(hashes) == 2
    return branch(hashes[0], hashes[1]), [bytes([LEAF_PQSCRIPT | 1]) + hashes[1], bytes([LEAF_PQSCRIPT | 1]) + hashes[0]]


def script_pubkey(root):
    return CScript([OP_1, root])


def sighash(tx, spent, leaf, key_version):
    msg = TaprootSignatureMsg(tx, spent, 0, 0, scriptpath=True, leaf_script=leaf, codeseparator_pos=0xFFFFFFFF, leaf_ver=LEAF_PQSCRIPT)
    # The framework writes key_version 0 (BIP 342); PQCH uses the algorithm number.
    msg = msg[:-5] + bytes([key_version]) + msg[-4:]
    return TaggedHash("TapSighash", msg)


def cmd_address(a):
    k = Keys(bytes.fromhex(a.seed))
    leaves = leaves_for(a.kind, k)
    root, _ = root_and_controls(leaves)
    print(json.dumps({
        "address": encode_segwit_address(a.hrp, 1, root),
        "scriptPubKey": bytes(script_pubkey(root)).hex(),
        "leaf_sizes": [len(bytes(s)) for s in leaves],
    }, indent=1))


def cmd_spend(a):
    k = Keys(bytes.fromhex(a.seed))
    leaves = leaves_for(a.kind, k)
    root, controls = root_and_controls(leaves)
    ver, prog = decode_segwit_address(a.hrp, a.to)
    assert ver is not None, "invalid destination address"
    tx = CTransaction()
    tx.version = 2
    tx.vin = [CTxIn(COutPoint(int(a.txid, 16), a.vout))]
    tx.vout = [CTxOut(a.amount - a.fee, CScript([CScriptOp.encode_op_n(ver), bytes(prog)]))]
    spent = [CTxOut(a.amount, script_pubkey(root))]
    leaf = leaves[a.leaf]
    if a.kind != "recovery" or a.leaf == 0:
        h = sighash(tx, spent, leaf, KEY_VERSION_MLDSA)
        mldsa_sig = ML_DSA_44.sign(k.mldsa_sk, h, ctx=CTX, deterministic=True)
        if a.corrupt:
            mldsa_sig = bytes([mldsa_sig[0] ^ 1]) + mldsa_sig[1:]
        stack = [mldsa_sig]
        if a.kind in ("hybrid", "recovery"):
            stack.append(sign_schnorr(k.ec_sk, sighash(tx, spent, leaf, 0)))
    else:
        h = sighash(tx, spent, leaf, KEY_VERSION_SLHDSA)
        m_prime = b"\x00" + bytes([len(CTX)]) + CTX + h
        sig = slh_low.sign(m_prime, k.slh_sk, sha2_128s, randomize=False)
        if a.corrupt:
            sig = bytes([sig[0] ^ 1]) + sig[1:]
        stack = [sig]
    wit = CTxInWitness()
    wit.scriptWitness.stack = stack + [bytes(leaf), controls[a.leaf]]
    tx.wit.vtxinwit = [wit]
    print(tx.serialize().hex())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("address", "spend"):
        s = sub.add_parser(name)
        s.add_argument("--seed", required=True)
        s.add_argument("--kind", choices=["mldsa", "hybrid", "recovery"], default="hybrid")
        s.add_argument("--hrp", default="pqcrt")
    s = sub.choices["spend"]
    s.add_argument("--txid", required=True)
    s.add_argument("--vout", type=int, required=True)
    s.add_argument("--amount", type=int, required=True, help="value of the spent output, in quanta")
    s.add_argument("--to", required=True)
    s.add_argument("--fee", type=int, default=100000)
    s.add_argument("--leaf", type=int, default=0)
    s.add_argument("--corrupt", action="store_true", help="flip a bit of the post-quantum signature")
    a = p.parse_args()
    {"address": cmd_address, "spend": cmd_spend}[a.cmd](a)


if __name__ == "__main__":
    main()
