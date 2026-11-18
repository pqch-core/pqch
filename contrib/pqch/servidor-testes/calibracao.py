#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
"""Dados da testnet para a calibracao da etapa 4 (contrib/pqch/CALIBRACAO.md).

Corre de hora a hora como pqch-vigia, depois da vigilancia. Le os blocos novos do no de vigilancia
e acrescenta uma linha por bloco a $ESTADO/blocos.csv; regista as cadeias concorrentes que o no ve
em $ESTADO/forks.csv; escreve um resumo em $ESTADO/calibracao.txt. Nao guarda enderecos IP.

  blocos.csv: altura, hash (16 hex), hora, intervalo_s (face ao bloco anterior), bits, dificuldade,
              auxpow (1 se minerado em conjunto com o Monero), chave_monero (16 primeiros hex)
  forks.csv:  hora_visto, altura, hash (16 hex), comprimento do ramo, estado
"""
import csv
import json
import os
import statistics
import subprocess
import sys
import time

ESTADO = os.environ.get("PQCH_VIGIA_ESTADO", "/var/lib/pqch-vigia")
CLI = [os.environ.get("PQCH_CLI", "/usr/local/bin/pqch-cli"), os.environ.get("PQCH_REDE", "-testnet"), f"-datadir={ESTADO}/no"]
JANELA_CHAVES = 4320   # blocos da janela das chaves Monero (FASE5.md)
LIMITE_CHAVES = 4
CAMPOS = ["altura", "hash", "hora", "intervalo_s", "bits", "dificuldade", "auxpow", "chave_monero"]


def rpc(*args):
    out = subprocess.run(CLI + [str(a) for a in args], capture_output=True, text=True, timeout=60)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip())
    return json.loads(out.stdout) if out.stdout.strip()[:1] in "[{" else out.stdout.strip()


def ler_csv(caminho):
    try:
        with open(caminho, newline="") as f:
            return list(csv.DictReader(f))
    except FileNotFoundError:
        return []


def acrescentar(caminho, campos, linhas):
    novo = not os.path.exists(caminho) or os.path.getsize(caminho) == 0
    with open(caminho, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=campos)
        if novo:
            w.writeheader()
        w.writerows(linhas)


def main():
    blocos_csv = os.path.join(ESTADO, "blocos.csv")
    forks_csv = os.path.join(ESTADO, "forks.csv")
    blocos = ler_csv(blocos_csv)
    try:
        tip = int(rpc("getblockcount"))
    except Exception as e:  # o no ainda nao responde: a vigilancia ja avisa
        print(f"[calibracao] no sem resposta: {e}", file=sys.stderr)
        return 0

    # 0. Se a cadeia se reorganizou desde a ultima vez, apaga os ultimos 100 blocos e le-os de novo.
    if blocos and rpc("getblockhash", int(blocos[-1]["altura"]))[:16] != blocos[-1].get("hash"):
        corte = int(blocos[-1]["altura"]) - 100
        blocos = [b for b in blocos if int(b["altura"]) < corte]
        with open(blocos_csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=CAMPOS)
            w.writeheader()
            w.writerows(blocos)

    # 1. Blocos novos (na primeira vez, so os ultimos 3 dias, para nao demorar).
    inicio = int(blocos[-1]["altura"]) + 1 if blocos else max(1, tip - JANELA_CHAVES)
    anterior = None
    if inicio > 1:
        anterior = rpc("getblockheader", rpc("getblockhash", inicio - 1))
    novos = []
    for h in range(inicio, tip + 1):
        cab = rpc("getblockheader", rpc("getblockhash", h))
        aux = cab.get("auxpow")
        novos.append({
            "altura": h,
            "hash": cab["hash"][:16],
            "hora": cab["time"],
            "intervalo_s": cab["time"] - anterior["time"] if anterior else "",
            "bits": cab["bits"],
            "dificuldade": f'{cab["difficulty"]:.6g}',
            "auxpow": 1 if aux else 0,
            "chave_monero": aux["seed_hash"][:16] if aux else "",
        })
        anterior = cab
    if novos:
        acrescentar(blocos_csv, CAMPOS, novos)
        blocos += [{k: str(v) for k, v in n.items()} for n in novos]

    # 2. Cadeias concorrentes (forks) que o no conhece.
    vistos = {f["hash"] for f in ler_csv(forks_csv)}
    forks = []
    for t in rpc("getchaintips"):
        if t["status"] == "active" or t["hash"][:16] in vistos:
            continue
        forks.append({"hora_visto": int(time.time()), "altura": t["height"], "hash": t["hash"][:16],
                      "comprimento": t["branchlen"], "estado": t["status"]})
    if forks:
        acrescentar(forks_csv, list(forks[0].keys()), forks)

    # 3. Resumo: ultimas 24 h e janela das chaves Monero.
    ult = blocos[-1440:]
    inter = [int(b["intervalo_s"]) for b in ult if b["intervalo_s"] not in ("", None)]
    janela = blocos[-JANELA_CHAVES:]
    chaves = {b["chave_monero"] for b in janela if b["chave_monero"]}
    todos_forks = ler_csv(forks_csv)
    linhas = [
        f"Atualizado: {time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime())}; altura {tip}",
        f"Ultimos {len(ult)} blocos: intervalo medio {statistics.mean(inter):.1f} s, mediana "
        f"{statistics.median(inter):.0f} s, 99% abaixo de {sorted(inter)[int(0.99 * (len(inter) - 1))]} s, "
        f"maior {max(inter)} s" if inter else "Ainda sem intervalos",
        f"Dificuldade: {ult[0]['dificuldade']} -> {ult[-1]['dificuldade']}" if ult else "",
        f"Minerados em conjunto com o Monero (24 h): {sum(int(b['auxpow']) for b in ult)} de {len(ult)}",
        f"Chaves Monero distintas nos ultimos {len(janela)} blocos: {len(chaves)} (limite {LIMITE_CHAVES} novas por {JANELA_CHAVES})",
        f"Cadeias concorrentes vistas desde o inicio: {len(todos_forks)}; maior ramo: "
        f"{max((int(f['comprimento']) for f in todos_forks), default=0)} blocos (limite de reorganizacao: 60)",
    ]
    with open(os.path.join(ESTADO, "calibracao.txt"), "w") as f:
        f.write("\n".join(l for l in linhas if l) + "\n")
    print(f"[calibracao] {len(novos)} blocos novos, {len(forks)} forks novos")
    return 0


if __name__ == "__main__":
    sys.exit(main())
