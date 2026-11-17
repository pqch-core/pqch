#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Fuzzing contínuo do PQCH (corre como o utilizador pqch-fuzz, serviço pqch-fuzz.service).
# Em cada volta: atualiza o código público (main), recompila se mudou, e corre cada alvo durante
# alguns minutos com o libFuzzer em modo fork. O corpus fica guardado entre voltas; as entradas que
# fazem o programa falhar ficam em $ESTADO/falhas/ e a volta continua com o alvo seguinte.
set -uo pipefail

SRC="${PQCH_FUZZ_SRC:-$HOME/pqch}"
ESTADO="${PQCH_FUZZ_ESTADO:-/var/lib/pqch-fuzz}"
JOBS="${PQCH_FUZZ_JOBS:-$(( $(nproc) > 1 ? $(nproc) - 1 : 1 ))}"
MIN_PQCH="${PQCH_FUZZ_MIN_PQCH:-45}"     # minutos por alvo do PQCH
MIN_HERDADO="${PQCH_FUZZ_MIN_HERDADO:-15}" # minutos por alvo herdado do Bitcoin Core

ALVOS_PQCH="pqch_pq_verify pqch_p2mr_descriptor pqch_auxpow_header pqch_auxpow_monero_block pqch_monero_primitives"
ALVOS_HERDADOS="headers_sync_state block_header pow pow_transition connect_block descriptor_parse mocked_descriptor_parse script_sign psbt script script_flags script_interpreter signature_checker miniscript_string miniscript_smart key"

registo() { echo "[fuzz] $*"; }

atualizar() {
    git -C "$SRC" fetch -q origin main || { registo "sem acesso ao GitHub; continuo com o código atual"; return 0; }
    local novo; novo="$(git -C "$SRC" rev-parse origin/main)"
    if [[ -x "$SRC/build_fuzz/bin/fuzz" && "$(cat "$ESTADO/commit" 2>/dev/null)" == "$novo" ]]; then
        return 0
    fi
    registo "a compilar ${novo:0:9}"
    git -C "$SRC" checkout -q --detach "$novo"
    ( cd "$SRC" && cmake --preset=libfuzzer -DENABLE_WALLET=OFF -DENABLE_IPC=OFF -DWITH_CCACHE=ON >/dev/null \
        && cmake --build build_fuzz -j"$JOBS" >/dev/null ) || { registo "a compilação falhou; continuo com o binário anterior"; return 0; }
    echo "$novo" > "$ESTADO/commit"
    registo "binário novo: ${novo:0:9}"
}

correr() {
    local alvo="$1" minutos="$2"
    mkdir -p "$ESTADO/corpus/$alvo" "$ESTADO/falhas"
    registo "$alvo: ${minutos} min, $JOBS processos"
    ( cd "$ESTADO" && FUZZ="$alvo" timeout --kill-after=60 $(( minutos * 60 + 120 )) \
        "$SRC/build_fuzz/bin/fuzz" -fork="$JOBS" -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1 \
        -max_total_time=$(( minutos * 60 )) -timeout=30 -rss_limit_mb=3000 \
        -artifact_prefix="$ESTADO/falhas/${alvo}-" "$ESTADO/corpus/$alvo" ) >"$ESTADO/ultimo.log" 2>&1
    local n; n="$(find "$ESTADO/falhas" -name "${alvo}-*" | wc -l)"
    registo "$alvo: terminado; entradas com falha guardadas para este alvo: $n"
}

mkdir -p "$ESTADO"
while true; do
    atualizar
    [[ -x "$SRC/build_fuzz/bin/fuzz" ]] || { registo "ainda sem binário; nova tentativa daqui a 10 min"; sleep 600; continue; }
    for a in $ALVOS_PQCH; do correr "$a" "$MIN_PQCH"; done
    for a in $ALVOS_HERDADOS; do correr "$a" "$MIN_HERDADO"; done
done
