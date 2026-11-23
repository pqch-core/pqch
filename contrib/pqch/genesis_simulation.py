#!/usr/bin/env python3
"""PQCH - simulacao da dificuldade inicial do genesis.

O ASERT do PQCH e absoluto e ancorado no genesis: o alvo de cada bloco depende so do alvo do
genesis (nBits), da hora do genesis e da altura. Esta simulacao responde a tres perguntas:

  1. Se a dificuldade do genesis for baixa demais para o hashrate do arranque, quantos blocos
     (e moedas) saem a mais, em quanto tempo, e para quem estiver a minerar logo no inicio?
  2. Se for alta demais, quanto tempo a rede fica lenta?
  3. O que acontece se a mineracao comecar depois da hora escrita no genesis?

Modelo (o mesmo do asert_simulation.py, com os valores do codigo):
  - T = 60 s, meia-vida do ASERT 4 h (consensus.nASERTHalfLife).
  - Trabalho esperado por bloco W = W_g * 2^-(prev_time - genesis_time - T*prev_height) / meia_vida,
    nunca abaixo do trabalho do powLimit (2^17 hashes; o alvo nao pode ser mais facil que o powLimit).
    E o CalculateASERT de src/pow.cpp com a ancora no genesis (parent_time = genesis_time - T).
  - W_g = H_g * T, onde H_g e o "hashrate de equilibrio do genesis": o hashrate que da blocos de
    60 s com a dificuldade do genesis. O genesis provisorio (nBits 0x1e7fffff = powLimit) tem
    H_g = 2^17 / 60 = ~2,2 kH/s.
  - O tempo de cada bloco e exponencial com media W / H(t). Os mineradores sao honestos (hora
    certa nos blocos).

Uso: genesis_simulation.py
"""
import math
import random
import statistics

T = 60.0
HALF_LIFE = 4 * 3600.0
W_MIN = 2.0 ** 17            # powLimit 0x1e7fffff: 2^256 / 2^239
SUBSIDY = 7                  # PQCH por bloco
SUPPLY = 58_900_000
RUNS = 40
DAY = 86400.0


def simulate(h_genesis, hashrate_fn, seed, start_delay=0.0, horizon=3 * DAY):
    """Blocos (tempo desde a hora do genesis, intervalo) ate 'horizon'."""
    rng = random.Random(seed)
    w_g = h_genesis * T
    prev_time = start_delay      # o primeiro bloco so pode comecar quando alguem minera
    height = 0                   # altura do bloco anterior (0 = genesis)
    out = []
    while prev_time < horizon:
        exponent = (prev_time - T * height) / HALF_LIFE
        work = max(W_MIN, w_g * 2.0 ** (-exponent))
        dt = rng.expovariate(1.0) * work / hashrate_fn(prev_time)
        prev_time += dt
        height += 1
        out.append((prev_time, dt))
    return out


def metrics(blocks, start_delay=0.0):
    def count_until(t):
        return sum(1 for x in blocks if x[0] <= t)
    first_hour = count_until(start_delay + 3600)
    day = count_until(DAY)
    # Blocos a mais (ou a menos) face a um bloco por minuto desde que a mineracao comecou.
    excess_day = day - (DAY - start_delay) / T
    # Quando acaba a rajada: hora a que o avanco sobre o calendario (altura - minutos desde o
    # arranque) chega a 90 % do maximo das primeiras 24 h. Com dificuldade alta demais (atraso),
    # hora a que a media de 60 blocos desce pela primeira vez a 66 s.
    ahead = [(x[0], i + 1 - (x[0] - start_delay) / T) for i, x in enumerate(blocks) if x[0] <= DAY]
    peak = max(a for _, a in ahead)
    if peak > 30:
        settle = next(t for t, a in ahead if a >= 0.9 * peak)
    else:
        settle = next((blocks[i][0] for i in range(60, len(blocks))
                       if (blocks[i][0] - blocks[i - 60][0]) / 60 <= 66), float("nan"))
    gaps_day = [x[1] for x in blocks if x[0] <= DAY]
    return {
        "first_hour": first_hour,
        "day": day,
        "excess_day": excess_day,
        "settle_h": max(0.0, settle - start_delay) / 3600,
        "max_gap_min": max(gaps_day) / 60 if gaps_day else float("nan"),
    }


def run(h_genesis, hashrate_fn, start_delay=0.0):
    rs = [metrics(simulate(h_genesis, hashrate_fn, s, start_delay), start_delay) for s in range(RUNS)]
    return {k: statistics.median(r[k] for r in rs) for k in rs[0]}


def fmt_rate(h):
    for unit, f in (("MH/s", 1e6), ("kH/s", 1e3)):
        if h >= f:
            return f"{h / f:g} {unit}"
    return f"{h:g} H/s"


def table(title, rows):
    print(f"\n## {title}\n")
    print("| Dificuldade do genesis (H_g) | Hashrate no arranque | Razao | Blocos na 1.a hora | "
          "Blocos a mais (1.as 24 h) | PQCH a mais | % da emissao total | Rajada acaba (90 %) / ritmo normal ao fim de | Maior espera em 24 h |")
    print("|---|---|---|---|---|---|---|---|---|")
    for label_g, label_h, ratio, m in rows:
        extra = max(0.0, m["excess_day"])
        print(f"| {label_g} | {label_h} | {ratio} | {m['first_hour']:.0f} | {m['excess_day']:+.0f} | "
              f"{extra * SUBSIDY:,.0f} | {extra * SUBSIDY / SUPPLY * 100:.3f} % | {m['settle_h']:.1f} h | "
              f"{m['max_gap_min']:.1f} min |")


def main():
    h_g_prov = W_MIN / T  # ~2185 H/s
    print("# Simulacao da dificuldade inicial do genesis (meia-vida 4 h, blocos de 60 s)")
    print(f"\nGenesis provisorio (powLimit): H_g = {h_g_prov:.0f} H/s. "
          f"Regra analitica: com hashrate constante R vezes H_g, saem a mais cerca de "
          f"meia-vida/T x log2(R) = {HALF_LIFE / T:.0f} x log2(R) blocos, quase todos nas primeiras horas.")

    # 1. Hashrate constante desde o arranque, varias dificuldades do genesis.
    rows = []
    for h_g in (h_g_prov, 20e3, 200e3, 2e6):
        for h in (2e3, 20e3, 200e3, 2e6, 20e6):
            r = h / h_g
            if not (0.05 <= r <= 2e4):
                continue
            rows.append((fmt_rate(h_g), fmt_rate(h), f"{r:g}x", run(h_g, lambda t, h=h: h)))
    table("1. Hashrate constante desde a hora do genesis", rows)

    # 2. Arranque realista: comeca com H0 e cresce 10x ao longo das primeiras 24 h (mais mineradores
    #    a juntar-se), depois fica constante.
    def ramp(h0):
        return lambda t: h0 * 10 ** min(1.0, t / DAY)
    rows = []
    for h_g in (h_g_prov, 20e3, 200e3):
        for h0 in (20e3, 200e3, 2e6):
            rows.append((fmt_rate(h_g), f"{fmt_rate(h0)} a subir 10x em 24 h", f"{h0 / h_g:g}x no inicio",
                         run(h_g, ramp(h0))))
    table("2. Hashrate a crescer 10x nas primeiras 24 h", rows)

    # 3. A mineracao comeca depois da hora do genesis (atraso). O ASERT ve a rede 'atrasada' e baixa
    #    a dificuldade 2x por cada meia-vida de atraso (ate ao powLimit).
    rows = []
    for delay_h in (0, 1, 4, 12):
        for h_g in (20e3, 200e3):
            m = run(h_g, lambda t, h=h_g: h, start_delay=delay_h * 3600)
            rows.append((fmt_rate(h_g), f"= H_g, comeca {delay_h} h depois", "1x", m))
    table("3. Mineracao comeca depois da hora do genesis", rows)


if __name__ == "__main__":
    main()
