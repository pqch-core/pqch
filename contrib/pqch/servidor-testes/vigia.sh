#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Vigilância da testnet (corre de hora a hora como pqch-vigia, pqch-vigia.timer).
# Usa um nó próprio, sem carteira e sem portas abertas, que entra na rede pelas sementes públicas
# como qualquer pessoa. Escreve uma linha por hora em $ESTADO/estado.csv. Não guarda endereços IP:
# só contagens. Quando algo falha, escreve um AVISO no registo do sistema (journalctl).
set -uo pipefail

ESTADO="${PQCH_VIGIA_ESTADO:-/var/lib/pqch-vigia}"
CLI=(pqch-cli -testnet -datadir="$ESTADO/no")
SEMENTES="187.127.89.83 72.61.20.80"
API="https://api-testnet.pqch.org/api"
TORNEIRA="https://faucet-testnet.pqch.org/"

avisos=()
porta() { timeout 6 bash -c "</dev/tcp/$1/39333" 2>/dev/null && echo 1 || echo 0; }

altura="$("${CLI[@]}" getblockcount 2>/dev/null || echo "")"
[[ -n "$altura" ]] || avisos+=("o nó local não responde")
ligacoes="$("${CLI[@]}" getconnectioncount 2>/dev/null || echo 0)"
# Nós conhecidos na rede (sem as duas sementes do projeto) e versões dos que estão ligados a nós.
read -r conhecidos versoes < <("${CLI[@]}" getnodeaddresses 0 2>/dev/null | python3 -c '
import json, sys
sementes = {"187.127.89.83", "72.61.20.80"}
try:
    nos = json.load(sys.stdin)
except Exception:
    nos = []
print(len({n["address"] for n in nos if n["address"] not in sementes}), end=" ")
' ; "${CLI[@]}" getpeerinfo 2>/dev/null | python3 -c '
import collections, json, sys
try:
    p = json.load(sys.stdin)
except Exception:
    p = []
c = collections.Counter(x.get("subver", "?").strip("/") for x in p)
print(";".join(f"{k}={v}" for k, v in sorted(c.items())) or "-")
')
altura_api="$(curl -fsS -m 15 "$API/blocks/tip/height" 2>/dev/null || echo "")"
[[ "$altura_api" =~ ^[0-9]+$ ]] || { avisos+=("a API da testnet não responde"); altura_api=""; }
torneira="$(curl -s -o /dev/null -m 15 -w '%{http_code}' "$TORNEIRA" 2>/dev/null)"
[[ "$torneira" == 200 ]] || avisos+=("a torneira respondeu ${torneira:-nada}")
portas=""
for s in $SEMENTES; do
    p="$(porta "$s")"; portas+="$p"
    [[ "$p" == 1 ]] || avisos+=("a semente $s não aceita ligações na 39333")
done
if [[ -n "$altura" && -n "$altura_api" ]] && (( altura - altura_api > 5 || altura_api - altura > 5 )); then
    avisos+=("a API está em $altura_api e o nó local em $altura")
fi

f="$ESTADO/estado.csv"
[[ -s "$f" ]] || echo "hora_utc,altura,altura_api,ligacoes,nos_conhecidos,versoes_ligadas,sementes_ok,torneira,avisos" > "$f"
echo "$(date -u +%FT%TZ),$altura,$altura_api,$ligacoes,$conhecidos,$versoes,$portas,$torneira,${#avisos[@]}" >> "$f"
for a in "${avisos[@]}"; do logger -t pqch-vigia -p user.warning "AVISO: $a"; done
