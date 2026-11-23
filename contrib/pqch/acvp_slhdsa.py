#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
"""Makes src/test/data/slhdsa_sha2_128s_acvp.json from the NIST ACVP-Server vectors (FIPS 205).

  git clone --depth 1 --filter=blob:none --sparse https://github.com/usnistgov/ACVP-Server
  cd ACVP-Server && git sparse-checkout set gen-val/json-files/SLH-DSA-keyGen-FIPS205 \\
      gen-val/json-files/SLH-DSA-sigGen-FIPS205 gen-val/json-files/SLH-DSA-sigVer-FIPS205
  contrib/pqch/acvp_slhdsa.py ACVP-Server/gen-val/json-files <commit> > src/test/data/slhdsa_sha2_128s_acvp.json

Keeps only SLH-DSA-SHA2-128s (the PQCH recovery key) and the pure "external" and "internal"
interfaces, the ones PQCH uses; the pre-hash ones are left out.
"""
import json
import sys

P = "SLH-DSA-SHA2-128s"
base, commit = sys.argv[1], sys.argv[2]


def groups(name):
    return [g for g in json.load(open(f"{base}/{name}/internalProjection.json"))["testGroups"]
            if g["parameterSet"] == P and g.get("preHash", "none") != "preHash"]


out = {"source": f"NIST ACVP-Server gen-val/json-files (commit {commit}), SLH-DSA FIPS 205, parameter set {P} only; "
                 "the pure and internal interfaces (the ones PQCH uses), not the pre-hash ones",
       "keygen": [], "siggen": [], "sigver": []}
for g in groups("SLH-DSA-keyGen-FIPS205"):
    out["keygen"] += [{k: t[k] for k in ("tcId", "skSeed", "skPrf", "pkSeed", "sk", "pk")} for t in g["tests"]]
for g in groups("SLH-DSA-sigGen-FIPS205"):
    for t in g["tests"]:
        out["siggen"].append({"tgId": g["tgId"], "tcId": t["tcId"], "interface": g["signatureInterface"], "deterministic": g["deterministic"],
                              "sk": t["sk"], "message": t["message"], "context": t.get("context", ""),
                              "additionalRandomness": t.get("additionalRandomness", ""), "signature": t["signature"]})
for g in groups("SLH-DSA-sigVer-FIPS205"):
    for t in g["tests"]:
        out["sigver"].append({"tgId": g["tgId"], "tcId": t["tcId"], "interface": g["signatureInterface"], "testPassed": t["testPassed"],
                              "reason": t.get("reason", ""), "pk": t["pk"], "message": t["message"], "context": t.get("context", ""),
                              "signature": t["signature"]})
print(json.dumps(out, separators=(",", ":")))
