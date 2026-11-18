#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
"""PQCH - simulacao de ataques de maioria e do limite de reorganizacao (60 blocos).

Corrida entre a cadeia honesta (fracao 1-q do hashrate) e uma cadeia privada do atacante (fracao q),
que comeca no bloco onde o atacante pagou a vitima. Conta o trabalho, nao o comprimento: com o
ASERT, o trabalho acumulado por segundo e o hashrate, seja qual for a dificuldade, por isso
manipular as horas dos blocos nao da trabalho a mais ao atacante. Cada bloco vale o mesmo trabalho
(a dificuldade nao muda durante a corrida, o que e conservador para a defesa).

Para cada caso mede:
  sem limite  - probabilidade de o atacante reverter um pagamento com z confirmacoes num prazo
  com limite  - o mesmo com o limite de 60 blocos: um no sincronizado recusa reorganizar mais de 60,
                por isso o atacante tem de passar a frente antes de a cadeia honesta ter 60 blocos
                depois do bloco onde a cadeia dele se separou
  separacao   - probabilidade de o atacante ter uma cadeia com mais trabalho, mas so depois dos 60
                blocos: os nos ja sincronizados recusam-na e os nos novos podem segui-la (a rede
                parte-se em duas ate alguem intervir)

Uso: ataque_simulation.py
"""
import random

RUNS = 20_000
HORIZON_BLOCKS = 1440      # o atacante tenta durante um dia de blocos honestos (1440 = 24 h)
LIMIT = 60                 # limite de reorganizacao (whitepaper, seccao 16)
CONFIRMATIONS = (1, 6, 10, 30, 60, 61)
SHARES = (0.30, 0.40, 0.51)


def race(q, z, rng):
    """Uma corrida. Devolve (reverte_sem_limite, reverte_com_limite, separa_a_rede).

    Blocos sucessivos sao do atacante com probabilidade q (processo de Poisson combinado).
    honest = blocos honestos depois do bloco da separacao; attacker = blocos do atacante.
    O pagamento esta no primeiro bloco honesto; tem z confirmacoes quando honest >= z.
    O atacante publica quando a cadeia dele tem mais trabalho (attacker > honest) e honest >= z.
    """
    honest = attacker = 0
    while honest < HORIZON_BLOCKS:
        if rng.random() < q:
            attacker += 1
        else:
            honest += 1
        if honest >= z and attacker > honest:
            depth = honest  # blocos honestos que os nos teriam de desfazer
            if depth <= LIMIT:
                return True, True, False
            return True, False, True
    return False, False, False


def main():
    rng = random.Random(2026)
    print(f"Corrida de {RUNS} tentativas por caso, durante {HORIZON_BLOCKS} blocos honestos (24 h).")
    print()
    print("| Hashrate do atacante | Confirmacoes | Reverte sem limite | Reverte com limite de 60 | Parte a rede (cadeia mais pesada recusada) |")
    print("| --- | --- | --- | --- | --- |")
    for q in SHARES:
        for z in CONFIRMATIONS:
            a = b = c = 0
            for _ in range(RUNS):
                x, y, w = race(q, z, rng)
                a += x
                b += y
                c += w
            print(f"| {q:.0%} | {z} | {a / RUNS:.2%} | {b / RUNS:.2%} | {c / RUNS:.2%} |")
    print()
    print("Leitura: com o limite, um pagamento com 61 ou mais confirmacoes (mais de uma hora) nunca e revertido")
    print("para os nos ja sincronizados; o risco que sobra e a rede partir-se quando um atacante com mais de metade")
    print("do hashrate publica uma cadeia mais pesada depois dos 60 blocos.")


if __name__ == "__main__":
    main()
