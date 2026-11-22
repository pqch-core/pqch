#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
"""Header checkpoints for src/kernel/chainparams.cpp (Consensus::Params::header_checkpoints).

Reads block hashes from a synced node with pqch-cli and prints the C++ lines, one checkpoint every
--intervalo blocks, up to the tip minus --margem blocks (deep enough that no reorganisation can touch
them: 1440 blocks are one day). Run it on a node you trust, before each release, and compare the
output with a second node (another machine, another operator) before committing it.

  contrib/pqch/checkpoints.py --rede testnet --cli "pqch-cli -datadir=/home/pqch/.pqch"
"""

import argparse
import shlex
import subprocess

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--rede", choices=["main", "testnet", "regtest"], default="main")
ap.add_argument("--cli", default="pqch-cli", help="pqch-cli command, with its options (datadir, rpcport, ...)")
ap.add_argument("--intervalo", type=int, default=500)
ap.add_argument("--margem", type=int, default=1440)
ap.add_argument("--ate", type=int, help="last height (so that two nodes give the same output)")
args = ap.parse_args()

cli = shlex.split(args.cli) + ([] if args.rede == "main" else [f"-{args.rede}"])


def rpc(*params):
    return subprocess.run(cli + [str(p) for p in params], check=True, capture_output=True, text=True).stdout.strip()


tip = int(rpc("getblockcount"))
last = (tip - args.margem) // args.intervalo * args.intervalo
if args.ate is not None:
    if args.ate > tip - args.margem:
        raise SystemExit(f"--ate {args.ate} is too close to the tip ({tip}); at most {tip - args.margem}")
    last = args.ate // args.intervalo * args.intervalo
print(f"        // Header checkpoints: every {args.intervalo} blocks up to {last}, contrib/pqch/checkpoints.py")
print("        consensus.header_checkpoints = {")
for height in range(args.intervalo, last + 1, args.intervalo):
    print(f'            {{{height}, uint256{{"{rpc("getblockhash", height)}"}}}},')
print("        };")
