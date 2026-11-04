#!/usr/bin/env python3
"""PQCH - simulacao do ASERT para calibrar a meia-vida.

Modelo: o tempo de cada bloco segue uma distribuicao exponencial com media
T / (H * t), onde T = 60 s, H e o hashrate relativo e t o alvo relativo
(t = 1 e o equilibrio com H = 1). O alvo segue o ASERT absoluto:
t = 2^((tempo_decorrido - T * (altura + 1)) / meia_vida).

Cenarios (cada um repetido com varias sementes):
  estavel    - hashrate constante: media e dispersao do tempo de bloco
  queda      - o hashrate cai para 1/10 (mineradores saem de repente)
  subida     - o hashrate sobe 10x (por exemplo, um grande pool de Monero adere)
  oportunista- um minerador com 2x o hashrate da rede entra quando a dificuldade
               esta abaixo da media e sai quando sobe (hash switching)

Uso: asert_simulation.py
"""
import math
import random
import statistics

T = 60.0
HALF_LIVES = {"2 h": 2 * 3600, "4 h": 4 * 3600, "8 h": 8 * 3600, "1 dia": 24 * 3600}
RUNS = 20


def simulate(half_life, hashrate_fn, n_blocks, seed):
    """Devolve listas de (tempo, intervalo, alvo relativo) por bloco."""
    rng = random.Random(seed)
    time = 0.0
    out = []
    for height in range(1, n_blocks + 1):
        # Alvo para este bloco a partir do bloco anterior (altura - 1).
        t = 2 ** ((time - T * height) / half_life)
        h = hashrate_fn(time, t)
        dt = rng.expovariate(1.0) * T / (h * t)
        time += dt
        out.append((time, dt, t, h))
    return out


def steady(half_life):
    cv_t, mean_dt, p99 = [], [], []
    for s in range(RUNS):
        r = simulate(half_life, lambda time, t: 1.0, 50_000, s)
        dts = [x[1] for x in r[5000:]]
        ts = [x[2] for x in r[5000:]]
        mean_dt.append(statistics.mean(dts))
        cv_t.append(statistics.pstdev(ts) / statistics.mean(ts))
        p99.append(sorted(dts)[int(0.99 * len(dts))])
    return statistics.mean(mean_dt), statistics.mean(cv_t) * 100, statistics.mean(p99)


def shock(half_life, factor):
    """Hashrate passa de 1 para 'factor' ao fim de 2 dias. Mede a recuperacao."""
    switch = 2 * 86400
    recover_h, extra_blocks, worst_gap = [], [], []
    for s in range(RUNS):
        r = simulate(half_life, lambda time, t: 1.0 if time < switch else factor, 20_000, s)
        after = [x for x in r if x[0] >= switch]
        eq = 1.0 / factor
        # Tempo ate o alvo ficar a menos de 10% do novo equilibrio.
        rec = next((x[0] for x in after if abs(x[2] / eq - 1) < 0.10), after[-1][0])
        recover_h.append((rec - switch) / 3600)
        # Blocos minerados nas 24 h seguintes acima (ou abaixo) do calendario de 1440.
        n24 = sum(1 for x in after if x[0] < switch + 86400)
        extra_blocks.append(n24 - 1440)
        worst_gap.append(max(x[1] for x in after[:2000]) / 60)
    return statistics.mean(recover_h), statistics.mean(extra_blocks), statistics.mean(worst_gap)


def hopper(half_life):
    """Minerador oportunista com 2x o hashrate base: entra se o alvo > media movel, sai caso contrario."""
    gains, mean_dt = [], []
    for s in range(RUNS):
        rng_state = {"avg": 1.0}

        def hr(time, t):
            # Media exponencial do alvo recente (o que o oportunista observa).
            rng_state["avg"] = 0.995 * rng_state["avg"] + 0.005 * t
            return 3.0 if t >= rng_state["avg"] else 1.0

        r = simulate(half_life, hr, 50_000, s)[5000:]
        mean_dt.append(statistics.mean(x[1] for x in r))
        # Ganho do oportunista: alvo medio dos blocos minerados enquanto esta ativo, face ao alvo
        # medio de todos os blocos (alvo maior = mineracao mais facil = mais recompensa por hash).
        on = [x[2] for x in r if x[3] > 1.0]
        allt = [x[2] for x in r]
        gains.append((statistics.mean(on) / statistics.mean(allt) - 1) * 100 if on else 0.0)
    return statistics.mean(mean_dt), statistics.mean(gains)


def main():
    print("| Meia-vida | Tempo medio (s) | Variacao da dificuldade | 99% dos blocos em menos de (s) |")
    print("| --- | --- | --- | --- |")
    for name, hl in HALF_LIVES.items():
        m, cv, p = steady(hl)
        print(f"| {name} | {m:.1f} | {cv:.1f}% | {p:.0f} |")
    print()
    print("| Meia-vida | Queda 10x: recuperacao (h) | Queda 10x: blocos em falta nas 24 h | Queda 10x: maior espera (min) | Subida 10x: recuperacao (h) | Subida 10x: blocos a mais nas 24 h |")
    print("| --- | --- | --- | --- | --- | --- |")
    for name, hl in HALF_LIVES.items():
        rd, bd, gd = shock(hl, 0.1)
        ru, bu, _ = shock(hl, 10.0)
        print(f"| {name} | {rd:.1f} | {-bd:.0f} | {gd:.0f} | {ru:.1f} | {bu:.0f} |")
    print()
    print("| Meia-vida | Oportunista: tempo medio (s) | Oportunista: vantagem do alvo (%) |")
    print("| --- | --- | --- |")
    for name, hl in HALF_LIVES.items():
        m, g = hopper(hl)
        print(f"| {name} | {m:.1f} | {g:+.2f}% |")


if __name__ == "__main__":
    main()
