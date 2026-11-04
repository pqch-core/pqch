#!/usr/bin/env python3
"""PQCH - implementacao de referencia do ASERT (aserti3) em Python.

Escrita a partir da especificacao do ASERT da Bitcoin Cash (2020-11-15, aserti3-2d),
de forma independente do codigo C++ em src/pow.cpp. Serve para:

  1. gerar vetores de teste usados em src/test/pqch_asert_tests.cpp;
  2. simular o comportamento da dificuldade (ver asert_simulation.py).

Uso:
  asert_reference.py vectors   -> imprime os vetores de teste em formato C++
"""
import random
import sys

POW_LIMIT = int("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 16)  # rede principal PQCH
SPACING = 60          # segundos por bloco
HALF_LIFE = 4 * 3600  # 4 horas

RADIX_BITS = 16
RADIX = 1 << RADIX_BITS


def bits_to_target(bits: int) -> int:
    """Equivalente a arith_uint256::SetCompact (sem sinal)."""
    size = bits >> 24
    word = bits & 0x007FFFFF
    if size <= 3:
        return word >> (8 * (3 - size))
    return word << (8 * (size - 3))


def target_to_bits(target: int) -> int:
    """Equivalente a arith_uint256::GetCompact."""
    size = (target.bit_length() + 7) // 8
    if size <= 3:
        compact = target << (8 * (3 - size))
    else:
        compact = target >> (8 * (size - 3))
    if compact & 0x00800000:
        compact >>= 8
        size += 1
    return compact | (size << 24)


def trunc_div(a: int, b: int) -> int:
    """Divisao inteira com truncatura em direcao a zero (como em C++)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def asert_target(ref_target: int, spacing: int, time_diff: int, height_diff: int,
                 pow_limit: int = POW_LIMIT, half_life: int = HALF_LIFE) -> int:
    """Alvo seguinte segundo o aserti3, apenas com aritmetica inteira."""
    assert 0 < ref_target <= pow_limit
    assert height_diff >= 0
    exponent = trunc_div((time_diff - spacing * (height_diff + 1)) * RADIX, half_life)
    num_shifts = exponent >> RADIX_BITS          # floor
    frac = exponent - num_shifts * RADIX         # em [0, 65536)
    assert 0 <= frac < RADIX
    poly = 195766423245049 * frac + 971821376 * frac ** 2 + 5127 * frac ** 3 + (1 << 47)
    assert poly < (1 << 64)  # a implementacao C++ usa inteiros de 64 bits sem sinal
    factor = (poly >> 48) + RADIX
    target = ref_target * factor
    if num_shifts < 0:
        target >>= -num_shifts
    else:
        target <<= num_shifts
    target >>= RADIX_BITS
    if target == 0:
        return 1
    return min(target, pow_limit)


def asert_bits(ref_bits: int, time_diff: int, height_diff: int, **kw) -> int:
    return target_to_bits(asert_target(bits_to_target(ref_bits), SPACING, time_diff, height_diff, **kw))


def vectors(seed: int = 20261104, n_random: int = 400):
    rng = random.Random(seed)
    genesis_bits = 0x1D00FFFF
    out = []
    # Casos de fronteira.
    for h in (0, 1, 2, 239, 240, 241, 1440, 525960, 2356000):
        out.append((genesis_bits, SPACING * (h + 1), h))                 # no horario: alvo igual
        out.append((genesis_bits, SPACING * (h + 1) + HALF_LIFE, h))     # 1 meia-vida atrasado: limite
        out.append((genesis_bits, SPACING * (h + 1) - HALF_LIFE, h))     # 1 meia-vida adiantado: alvo /2
        out.append((0x1B0404CB, SPACING * (h + 1) + HALF_LIFE, h))       # alvo * 2
        out.append((0x1B0404CB, SPACING * (h + 1) - 3 * HALF_LIFE, h))   # alvo / 8
    out.append((0x1B0404CB, -10**9, 10**6))   # muito adiantado: tende para o alvo mais dificil
    out.append((0x1B0404CB, 10**9, 0))         # muito atrasado: limite (powLimit)
    out.append((0x03000001, -10**6, 10**4))    # alvo minusculo: nunca abaixo de 1
    # Fracoes da meia-vida (testa o polinomio).
    for k in range(0, 65536, 4099):
        out.append((0x1B0404CB, SPACING + trunc_div(k * HALF_LIFE, RADIX), 0))
    # Casos aleatorios.
    for _ in range(n_random):
        ref_target = rng.randint(1 << 160, POW_LIMIT)
        ref_bits = target_to_bits(ref_target)
        if bits_to_target(ref_bits) == 0 or bits_to_target(ref_bits) > POW_LIMIT:
            continue
        height = rng.choice([rng.randint(0, 1000), rng.randint(0, 10**7)])
        drift = rng.choice([rng.randint(-HALF_LIFE * 4, HALF_LIFE * 4), rng.randint(-10**7, 10**7)])
        out.append((ref_bits, SPACING * (height + 1) + drift, height))
    return [(b, t, h, asert_bits(b, t, h)) for (b, t, h) in out]


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "vectors":
        print("// Gerado por contrib/pqch/asert_reference.py vectors (seed 20261104). Nao editar a mao.")
        print("// {ref_bits, time_diff, height_diff, expected_bits}")
        for b, t, h, e in vectors():
            print(f"    {{0x{b:08x}, {t}, {h}, 0x{e:08x}}},")
    else:
        print(__doc__)
