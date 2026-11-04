#!/usr/bin/env bash
# PQCH - validacao da Fase 1 em regtest.
#
# Mineira a curva de emissao completa da regtest (halving a cada 150 blocos)
# e confirma que:
#   1. o bloco genesis e o da PQCH;
#   2. os enderecos usam o prefixo pqcrt1;
#   3. a recompensa comeca em 12,5 PQCH e cai para metade em cada halving;
#   4. a oferta total no conjunto UTXO coincide com o valor calculado;
#   5. depois do ultimo halving, os blocos deixam de criar moedas.
#
# Uso: contrib/pqch/validar_fase1.sh [pasta_de_build]
set -euo pipefail

BUILD="${1:-build}"
BITCOIND="$BUILD/bin/bitcoind"
CLI="$BUILD/bin/bitcoin-cli"
DATADIR="$(mktemp -d)"
GENESIS_ESPERADO="126fed0ca2429d13f7515bd67256a010767ad26d35bf9acbaecf11d998503e79"
HALVING=150          # regtest
SUBSIDIO=1250000000  # 12,5 PQCH em quanta

cli() { "$CLI" -regtest -datadir="$DATADIR" "$@"; }
parar() { cli stop >/dev/null 2>&1 || true; sleep 2; rm -rf "$DATADIR"; }
trap parar EXIT

"$BITCOIND" -regtest -datadir="$DATADIR" -daemon -listen=0 -fallbackfee=0 >/dev/null
for _ in $(seq 1 60); do cli getblockcount >/dev/null 2>&1 && break; sleep 1; done

falhas=0
verifica() { # descricao, obtido, esperado
    if [[ "$2" == "$3" ]]; then echo "OK    $1: $2"; else echo "FALHA $1: obtido $2, esperado $3"; falhas=$((falhas+1)); fi
}

verifica "Bloco genesis" "$(cli getblockhash 0)" "$GENESIS_ESPERADO"

# Endereco de teste (chave publica de exemplo, sem valor real).
DESC="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)"
DESC="$(cli getdescriptorinfo "$DESC" | python3 -c 'import json,sys; print(json.load(sys.stdin)["descriptor"])')"
ENDERECO="$(cli deriveaddresses "$DESC" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0])')"
verifica "Prefixo do endereco" "${ENDERECO:0:6}" "pqcrt1"

# Curva completa: o subsidio chega a zero ao fim de 31 halvings.
TOTAL_BLOCOS=$((HALVING * 32 + 10))
echo "A minerar $TOTAL_BLOCOS blocos..."
restante=$TOTAL_BLOCOS
while (( restante > 0 )); do
    n=$(( restante > 500 ? 500 : restante ))
    cli generatetoaddress "$n" "$ENDERECO" >/dev/null
    restante=$((restante - n))
done
verifica "Altura" "$(cli getblockcount)" "$TOTAL_BLOCOS"

recompensa() { # altura -> valor da coinbase em quanta
    local h; h="$(cli getblockhash "$1")"
    cli getblock "$h" 2 | python3 -c '
import json,sys
from decimal import Decimal
b=json.load(sys.stdin)
print(int(sum(Decimal(str(o["value"])) for o in b["tx"][0]["vout"]) * 100000000))'
}
for h in 1 $((HALVING-1)) $HALVING $((HALVING*2)) $((HALVING*30)) $((HALVING*31)); do
    k=$((h / HALVING))
    esperado=$(( k >= 64 ? 0 : SUBSIDIO >> k ))
    verifica "Recompensa no bloco $h (halving $k)" "$(recompensa "$h")" "$esperado"
done

# Oferta total esperada: soma de todas as recompensas desde o bloco 1
# (a saida do genesis nao entra no conjunto UTXO).
ESPERADO=$(python3 -c "
H=$HALVING; r=$SUBSIDIO; n=$TOTAL_BLOCOS
print(sum((r >> (h//H)) if h//H < 64 else 0 for h in range(1, n+1)))")
OBTIDO=$(cli gettxoutsetinfo | python3 -c '
import json,sys
from decimal import Decimal
print(int(Decimal(str(json.load(sys.stdin)["total_amount"])) * 100000000))')
verifica "Oferta total no conjunto UTXO (quanta)" "$OBTIDO" "$ESPERADO"

echo
if (( falhas == 0 )); then echo "Fase 1 validada em regtest."; else echo "$falhas verificacao(oes) falharam."; exit 1; fi
