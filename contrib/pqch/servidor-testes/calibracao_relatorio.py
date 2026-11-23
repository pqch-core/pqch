#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
"""Relatorio para a decisao dos quatro parametros (contrib/pqch/CALIBRACAO.md, secao 3).

Le $ESTADO/blocos.csv e $ESTADO/forks.csv da maquina de vigilancia e, para os blocos minerados em
conjunto, pergunta ao no de vigilancia o tamanho do tx_extra. So imprime contagens e medidas: nada de
enderecos IP nem de hashes completos. Uso (na maquina de vigilancia):

  sudo -u pqch-vigia python3 calibracao_relatorio.py [--desde 'AAAA-MM-DD HH:MM'] [--ate ...]
"""
import argparse
import bisect
import calendar
import csv
import json
import os
import statistics
import subprocess
import time

ESTADO = os.environ.get("PQCH_VIGIA_ESTADO", "/var/lib/pqch-vigia")
CLI = [os.environ.get("PQCH_CLI", "/usr/local/bin/pqch-cli"), os.environ.get("PQCH_REDE", "-testnet"), f"-datadir={ESTADO}/no"]
JANELA_CHAVES = 4320
T = 60


def ler(nome):
    try:
        with open(os.path.join(ESTADO, nome), newline="") as f:
            return list(csv.DictReader(f))
    except FileNotFoundError:
        return []


def rpc(*args):
    out = subprocess.run(CLI + [str(a) for a in args], capture_output=True, text=True, timeout=60)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip())
    return json.loads(out.stdout) if out.stdout.strip()[:1] in "[{" else out.stdout.strip()


def hora_utc(s):
    return calendar.timegm(time.strptime(s, "%Y-%m-%d %H:%M"))


def pct(xs, p):
    xs = sorted(xs)
    return xs[int(p * (len(xs) - 1))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--desde", help="UTC, 'AAAA-MM-DD HH:MM'")
    ap.add_argument("--ate", help="UTC, 'AAAA-MM-DD HH:MM'")
    ap.add_argument("--sem-rpc", action="store_true", help="nao perguntar o tx_extra ao no")
    ap.add_argument("--janela", type=int, default=0, help="minutos: blocos e dificuldade media por janela (teste de hashrate)")
    a = ap.parse_args()

    blocos = ler("blocos.csv")
    for b in blocos:
        b["t"] = int(b["hora"])
        b["h"] = int(b["altura"])
    blocos.sort(key=lambda b: b["h"])
    if a.desde:
        blocos = [b for b in blocos if b["t"] >= hora_utc(a.desde)]
    if a.ate:
        blocos = [b for b in blocos if b["t"] <= hora_utc(a.ate)]
    if len(blocos) < 2:
        print("Sem blocos suficientes")
        return
    t0, t1 = blocos[0]["t"], blocos[-1]["t"]
    print(f"== Blocos {blocos[0]['h']} a {blocos[-1]['h']} ({len(blocos)}), "
          f"{time.strftime('%Y-%m-%d %H:%M', time.gmtime(t0))} a {time.strftime('%Y-%m-%d %H:%M', time.gmtime(t1))} UTC "
          f"({(t1 - t0) / 86400:.1f} dias)")

    # 1. Meia-vida: intervalos, dias, quedas abaixo de metade do ritmo, oscilacao da dificuldade.
    inter = [int(b["intervalo_s"]) for b in blocos[1:] if b["intervalo_s"] not in ("", None)]
    print("\n-- 1. Meia-vida do ASERT")
    print(f"Intervalo: media {statistics.mean(inter):.1f} s, mediana {statistics.median(inter):.0f} s, "
          f"p99 {pct(inter, 0.99)} s, maior {max(inter)} s; ritmo global "
          f"{(t1 - t0) / (len(blocos) - 1):.1f} s por bloco")
    dias = {}
    for b in blocos:
        dias.setdefault(time.strftime("%Y-%m-%d", time.gmtime(b["t"])), []).append(b)
    print("Dia (UTC)   blocos  intervalo  dificuldade min..max (variacao)  em conjunto")
    for d, bs in sorted(dias.items()):
        dif = [float(b["dificuldade"]) for b in bs]
        dur = bs[-1]["t"] - bs[0]["t"]
        media = dur / (len(bs) - 1) if len(bs) > 1 else float("nan")
        var = statistics.pstdev(dif) / statistics.mean(dif) * 100
        print(f"{d}  {len(bs):6d}  {media:7.1f} s  {min(dif):12.6g}..{max(dif):<12.6g} ({var:4.1f} %)  "
              f"{sum(int(b['auxpow']) for b in bs):5d}")
    # Periodos em que a ultima hora teve menos de 30 blocos (metade do ritmo).
    ts = [b["t"] for b in blocos]
    lento, inicio, maior, n_periodos = 0, None, 0, 0
    passo = 300
    for t in range(t0 + 3600, t1 + 1, passo):
        n = bisect.bisect_right(ts, t) - bisect.bisect_left(ts, t - 3600)
        if n < 30:
            if inicio is None:
                inicio = t
                n_periodos += 1
            lento += passo
        elif inicio is not None:
            maior = max(maior, t - inicio)
            inicio = None
    if inicio is not None:
        maior = max(maior, t1 - inicio)
    print(f"Abaixo de metade do ritmo (menos de 30 blocos na ultima hora): {n_periodos} periodos, "
          f"{lento / 3600:.1f} h no total, o maior {maior / 3600:.1f} h (criterio: nenhum acima de 2 h)")
    # Semanas: criterio 55 a 65 s.
    semanas = {}
    for b in blocos:
        semanas.setdefault(time.strftime("%G-S%V", time.gmtime(b["t"])), []).append(b["t"])
    for s, xs in sorted(semanas.items()):
        if len(xs) > 1:
            print(f"Semana {s}: {(xs[-1] - xs[0]) / (len(xs) - 1):.1f} s por bloco ({len(xs)} blocos)")

    if a.janela:
        passo_j = a.janela * 60
        print(f"Por janelas de {a.janela} min (UTC): blocos, intervalo medio, dificuldade media, em conjunto")
        inicio_j = t0 - t0 % passo_j
        while inicio_j <= t1:
            bs = [b for b in blocos if inicio_j <= b["t"] < inicio_j + passo_j]
            if bs:
                dif = statistics.mean(float(b["dificuldade"]) for b in bs)
                print(f"  {time.strftime('%H:%M', time.gmtime(inicio_j))}  {len(bs):4d}  "
                      f"{passo_j / len(bs):6.1f} s  {dif:.6g}  {sum(int(b['auxpow']) for b in bs)}")
            else:
                print(f"  {time.strftime('%H:%M', time.gmtime(inicio_j))}     0")
            inicio_j += passo_j

    # 2. Limite de reorganizacao.
    forks = ler("forks.csv")
    comp = [int(f["comprimento"]) for f in forks]
    print("\n-- 2. Limite de reorganizacao (60)")
    if comp:
        dist = {c: comp.count(c) for c in sorted(set(comp))}
        print(f"Cadeias concorrentes: {len(comp)}; maior ramo {max(comp)}; por comprimento: {dist}")
    else:
        print("Cadeias concorrentes: 0")

    # 3. Janela das chaves Monero.
    print("\n-- 3. Janela das chaves Monero (4 novas por 4.320 blocos)")
    aux = [b for b in blocos if b["auxpow"] == "1"]
    print(f"Blocos minerados em conjunto: {len(aux)} de {len(blocos)}")
    chaves = [b["chave_monero"] for b in blocos]
    maximo, onde, conta = 0, None, {}
    for i, c in enumerate(chaves):
        if c:
            conta[c] = conta.get(c, 0) + 1
        if i >= JANELA_CHAVES:
            velha = chaves[i - JANELA_CHAVES]
            if velha:
                conta[velha] -= 1
                if conta[velha] == 0:
                    del conta[velha]
        if len(conta) > maximo:
            maximo, onde = len(conta), blocos[i]["h"]
    print(f"Maximo de chaves distintas numa janela: {maximo}" + (f" (acaba na altura {onde})" if onde else ""))
    ordem = []
    for b in aux:
        if b["chave_monero"] not in [c for c, _ in ordem]:
            ordem.append((b["chave_monero"], b["h"]))
    for c, h in ordem:
        print(f"  chave {c}…: primeira vez na altura {h}")

    # 4. tx_extra das provas reais.
    print("\n-- 4. tx_extra (limite 512 B)")
    if a.sem_rpc or not aux:
        print("Sem consulta ao no" if a.sem_rpc else "Sem blocos minerados em conjunto")
        return
    tamanhos, falhas = [], 0
    for b in aux[-500:]:
        try:
            cab = rpc("getblockheader", rpc("getblockhash", b["h"]))
            extra = cab.get("auxpow", {}).get("coinbase_extra")
            if extra is None:
                falhas += 1
                continue
            tamanhos.append(len(extra) // 2)
        except Exception:
            falhas += 1
    if tamanhos:
        print(f"{len(tamanhos)} provas: tx_extra min {min(tamanhos)} B, mediana {statistics.median(tamanhos):.0f} B, "
              f"maior {max(tamanhos)} B (folga {512 - max(tamanhos)} B)")
    if falhas:
        print(f"{falhas} blocos sem auxpow no getblockheader (no antigo ou ramo lateral)")


if __name__ == "__main__":
    main()
