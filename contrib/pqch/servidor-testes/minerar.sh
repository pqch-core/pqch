#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Minerador da testnet (corre como pqch-minerador, serviço pqch-minerar-testnet.service).
# Espera que o nó esteja sincronizado, abre (ou cria) a carteira "mineracao", guarda um endereço
# de recompensa e pede um bloco de cada vez com generatetoaddress. Só moedas de teste.
set -uo pipefail

DADOS="${PQCH_MINERADOR_DADOS:-/var/lib/pqch-minerador}"
CLI=(pqch-cli -testnet -datadir="$DADOS" -rpcwait)

"${CLI[@]}" loadwallet mineracao >/dev/null 2>&1 || "${CLI[@]}" createwallet mineracao >/dev/null 2>&1 || true
if [[ ! -s "$DADOS/endereco-recompensa" ]]; then
    "${CLI[@]}" -rpcwallet=mineracao getnewaddress recompensa > "$DADOS/endereco-recompensa"
fi
ENDERECO="$(cat "$DADOS/endereco-recompensa")"
echo "[minerar] recompensas para $ENDERECO"

while [[ "$("${CLI[@]}" getblockchaininfo | python3 -c 'import json,sys; print(json.load(sys.stdin)["initialblockdownload"])')" == True ]]; do
    echo "[minerar] à espera de o nó sincronizar"; sleep 60
done
echo "[minerar] a minerar"
while true; do
    if ! out="$("${CLI[@]}" generatetoaddress 1 "$ENDERECO" 2>&1)"; then
        echo "[minerar] erro: $out"; sleep 30
    elif [[ "$out" != "[]" ]]; then
        echo "[minerar] bloco encontrado: $(tr -d '[]" \n' <<<"$out" | cut -c1-16)… altura $("${CLI[@]}" getblockcount)"
    fi
done
