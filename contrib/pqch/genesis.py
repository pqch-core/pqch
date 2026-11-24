#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
"""Génesis da PQCH: calcula o id do bloco e a raiz de Merkle e, com --aplicar, escreve-os no código.

Faz o mesmo que CreatePQCHGenesisBlock em src/kernel/chainparams.cpp:
  - coinbase versão 1, uma entrada nula, scriptSig = <486604799> <4> <mensagem>;
  - uma saída de `recompensa` quanta para OP_RETURN "PQCH" (não se pode gastar);
  - cabeçalho versão 1, bloco anterior nulo, nonce 0. O id é o SHA256d dos 80 bytes. O génesis não
    tem prova de trabalho.

Uso:
  genesis.py --verificar
      confere a ferramenta contra os génesis que já estão no código (testnet e rede principal
      provisória);
  genesis.py --mensagem "..." --hora 2026-11-27T23:00:00Z [--bits 0x1e7fffff] [--recompensa 7]
      mostra o id e a raiz de Merkle;
  genesis.py ... --aplicar
      substitui o génesis da rede principal em src/kernel/chainparams.cpp (mensagem, hora, bits,
      recompensa e os dois asserts). Não mexe na testnet nem no regtest.
"""
import argparse
import calendar
import hashlib
import re
import struct
import sys
import time
from pathlib import Path

COIN = 100_000_000
CHAINPARAMS = Path(__file__).resolve().parents[2] / "src" / "kernel" / "chainparams.cpp"


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def push(data):
    n = len(data)
    if n < 0x4c:
        return bytes([n]) + data
    if n <= 0xff:
        return b"\x4c" + bytes([n]) + data
    return b"\x4d" + struct.pack("<H", n) + data


def compact_size(n):
    if n < 0xfd:
        return bytes([n])
    if n <= 0xffff:
        return b"\xfd" + struct.pack("<H", n)
    return b"\xfe" + struct.pack("<I", n)


def genesis(message, ntime, nbits, reward):
    msg = message.encode("utf-8")
    script_sig = push(bytes.fromhex("ffff001d")) + push(b"\x04") + push(msg)  # 486604799, CScriptNum(4), mensagem
    script_pubkey = b"\x6a" + push(b"PQCH")                                    # OP_RETURN "PQCH"
    tx = (struct.pack("<i", 1) + compact_size(1) + b"\x00" * 32 + struct.pack("<I", 0xFFFFFFFF)
          + compact_size(len(script_sig)) + script_sig + struct.pack("<I", 0xFFFFFFFF)
          + compact_size(1) + struct.pack("<q", reward) + compact_size(len(script_pubkey)) + script_pubkey
          + struct.pack("<I", 0))
    merkle = sha256d(tx)  # uma só transação: a raiz é o txid
    header = struct.pack("<i", 1) + b"\x00" * 32 + merkle + struct.pack("<III", ntime, nbits, 0)
    return sha256d(header)[::-1].hex(), merkle[::-1].hex()


def verificar():
    src = CHAINPARAMS.read_text(encoding="utf-8")
    casos = [
        ("testnet", re.search(r'PQCH_TESTNET_GENESIS_MESSAGE\{"([^"]+)"\}', src).group(1),
         int(re.search(r"PQCH_TESTNET_GENESIS_TIME\{(\d+)\}", src).group(1)), 0x1e7fffff, 1_250_000_000,
         "b3079ef1e4520057cda8cfb841850523d295519c1815a8e3287e2a9c465d9caa",
         "c39254a1cdab114a1e14d2fe8a31d686801052099170e132db6b2109109ab93c"),
    ]
    m = re.search(r'genesis = CreatePQCHGenesisBlock\("([^"]+)", (\d+), 0, (0x[0-9a-f]+), ([0-9\']+)\);\n'
                  r'\s+consensus.hashGenesisBlock = genesis.GetHash\(\);\n'
                  r'\s+assert\(consensus.hashGenesisBlock == uint256\{"([0-9a-f]{64})"\}\);\n'
                  r'\s+assert\(genesis.hashMerkleRoot == uint256\{"([0-9a-f]{64})"\}\);', src)
    casos.append(("rede principal (no código)", m.group(1), int(m.group(2)), int(m.group(3), 16),
                  int(m.group(4).replace("'", "")), m.group(5), m.group(6)))
    ok = True
    for nome, msg, t, bits, reward, h_esp, m_esp in casos:
        h, mr = genesis(msg, t, bits, reward)
        certo = (h == h_esp and mr == m_esp)
        ok &= certo
        print(f"{'OK ' if certo else 'ERRO'} {nome}: id {h}, merkle {mr}")
    return ok


def aplicar(message, ntime, nbits, reward_q, h, mr):
    src = CHAINPARAMS.read_text(encoding="utf-8")
    padrao = re.compile(r'(genesis = CreatePQCHGenesisBlock\()"[^"]+", \d+, 0, 0x[0-9a-f]+, [0-9\']+(\);\n'
                        r'\s+consensus.hashGenesisBlock = genesis.GetHash\(\);\n'
                        r'\s+assert\(consensus.hashGenesisBlock == uint256\{")[0-9a-f]{64}("\}\);\n'
                        r'\s+assert\(genesis.hashMerkleRoot == uint256\{")[0-9a-f]{64}("\}\);)')
    achados = padrao.findall(src)
    if len(achados) != 1:
        sys.exit(f"esperava um único génesis da rede principal escrito por extenso, encontrei {len(achados)}")
    if '"' in message or "\\" in message:
        sys.exit("a mensagem não pode ter aspas nem barras invertidas")
    rec = f"{reward_q:_}".replace("_", "'")
    novo = padrao.sub(lambda mm: f'{mm.group(1)}"{message}", {ntime}, 0, 0x{nbits:08x}, {rec}{mm.group(2)}{h}{mm.group(3)}{mr}{mm.group(4)}', src)
    CHAINPARAMS.write_text(novo, encoding="utf-8")
    print(f"escrito em {CHAINPARAMS}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verificar", action="store_true")
    ap.add_argument("--mensagem")
    ap.add_argument("--hora", help="UTC, 2026-11-27T23:00:00Z")
    ap.add_argument("--bits", default="0x1e7fffff")
    ap.add_argument("--recompensa", type=float, default=7, help="em PQCH (por omissão 7)")
    ap.add_argument("--aplicar", action="store_true")
    a = ap.parse_args()
    if a.verificar:
        sys.exit(0 if verificar() else 1)
    if not (a.mensagem and a.hora):
        ap.error("indica --mensagem e --hora (ou --verificar)")
    ntime = calendar.timegm(time.strptime(a.hora, "%Y-%m-%dT%H:%M:%SZ"))
    nbits = int(a.bits, 16)
    reward = round(a.recompensa * COIN)
    h, mr = genesis(a.mensagem, ntime, nbits, reward)
    print(f"mensagem:   {a.mensagem}")
    print(f"hora:       {ntime} ({time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(ntime))})")
    print(f"bits:       0x{nbits:08x}")
    print(f"recompensa: {reward} quanta")
    print(f"id:         {h}")
    print(f"merkle:     {mr}")
    if a.aplicar:
        aplicar(a.mensagem, ntime, nbits, reward, h, mr)


if __name__ == "__main__":
    main()
