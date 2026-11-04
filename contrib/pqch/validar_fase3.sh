#!/usr/bin/env bash
# PQCH - validacao da Fase 3: prova de trabalho RandomX v2.
#
# Parte A (regtest, 3 nos): o no A mineira com RandomX, os nos B e C recebem e validam
#   os blocos pela rede P2P e todos acabam na mesma ponta da cadeia.
# Parte B (rede principal provisoria, isolada, 2 nos): mineracao RandomX a dificuldade
#   real (limite: ~131.072 hashes por bloco) com o ASERT ativo; o segundo no valida e
#   rejeita um bloco com prova de trabalho falsa.
#
# Uso: contrib/pqch/validar_fase3.sh [pasta_de_build] [blocos_na_rede_principal]
set -euo pipefail

BUILD="${1:-build}"
MAIN_BLOCKS="${2:-3}"
BITCOIND="$BUILD/bin/bitcoind"
CLI="$BUILD/bin/bitcoin-cli"
TMP="$(mktemp -d)"
PIDS=()

falhas=0
verifica() { if [[ "$2" == "$3" ]]; then echo "OK    $1: $2"; else echo "FALHA $1: obtido $2, esperado $3"; falhas=$((falhas+1)); fi; }
limpar() { for d in "$TMP"/*/; do "$CLI" -datadir="$d" stop >/dev/null 2>&1 || true; done; sleep 3; rm -rf "$TMP"; }
trap limpar EXIT

# arranca um no: nome, rede (regtest|main), porta p2p, porta rpc, [ligar a porta]
arranca() {
    local nome=$1 rede=$2 p2p=$3 rpc=$4 ligar=${5:-}
    local dir="$TMP/$nome"; mkdir -p "$dir"
    {
        [[ "$rede" == regtest ]] && echo "regtest=1"
        echo "server=1"; echo "rpcuser=pqch"; echo "rpcpassword=pqch"
        echo "dnsseed=0"; echo "fixedseeds=0"; echo "listenonion=0"; echo "discover=0"
        [[ "$rede" == regtest ]] && echo "[regtest]" || echo "[main]"
        echo "port=$p2p"; echo "rpcport=$rpc"; echo "bind=127.0.0.1"
        [[ -n "$ligar" ]] && echo "connect=127.0.0.1:$ligar"
    } > "$dir/bitcoin.conf"
    "$BITCOIND" -datadir="$dir" -daemon -debug=validation >/dev/null
    for _ in $(seq 1 60); do "$CLI" -datadir="$dir" getblockcount >/dev/null 2>&1 && return; sleep 1; done
    echo "no $nome nao arrancou"; exit 1
}
c() { local n=$1; shift; "$CLI" -datadir="$TMP/$n" "$@"; }
espera_altura() { # no, altura, segundos
    for _ in $(seq 1 "$3"); do [[ "$(c "$1" getblockcount)" -ge "$2" ]] && return 0; sleep 1; done; return 1; }

DESC="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)"
endereco() { local d; d="$(c "$1" getdescriptorinfo "$DESC" | python3 -c 'import json,sys;print(json.load(sys.stdin)["descriptor"])')"; c "$1" deriveaddresses "$d" | python3 -c 'import json,sys;print(json.load(sys.stdin)[0])'; }

echo "=== Parte A: regtest, 3 nos ==="
arranca A regtest 49544 49543
arranca B regtest 49554 49553 49544
arranca C regtest 49564 49563 49554
sleep 3
verifica "Ligacoes do no B" "$(c B getconnectioncount)" "2"
END_A="$(endereco A)"
inicio=$(date +%s.%N)
c A generatetoaddress 200 "$END_A" >/dev/null
fim=$(date +%s.%N)
echo "      200 blocos minerados no no A em $(python3 -c "print(f'{$fim-$inicio:.1f}')") s (RandomX, modo leve)"
espera_altura B 200 60 && espera_altura C 200 60 || true
verifica "Altura no no B" "$(c B getblockcount)" "200"
verifica "Altura no no C" "$(c C getblockcount)" "200"
verifica "Mesma ponta em A e C" "$(c A getbestblockhash)" "$(c C getbestblockhash)"
GBT=$(c A getblocktemplate '{"rules":["segwit"]}')
verifica "Chave RandomX no getblocktemplate" "$(echo "$GBT" | python3 -c 'import json,sys;print(json.load(sys.stdin)["randomx_seed_hash"])')" "$(c A getblockhash 0)"

echo
echo "=== Parte B: rede principal provisoria, isolada, dificuldade real ==="
arranca M1 main 29433 29432
arranca M2 main 29443 29442 29433
sleep 3
END_M="$(c M1 deriveaddresses "$(c M1 getdescriptorinfo "$DESC" | python3 -c 'import json,sys;print(json.load(sys.stdin)["descriptor"])')" | python3 -c 'import json,sys;print(json.load(sys.stdin)[0])')"
verifica "Prefixo de endereco na rede principal" "${END_M:0:4}" "pqc1"
verifica "Alvo inicial (limite RandomX)" "$(c M1 getblocktemplate '{"rules":["segwit"]}' | python3 -c 'import json,sys;print(json.load(sys.stdin)["bits"])')" "1e7fffff"
for i in $(seq 1 "$MAIN_BLOCKS"); do
    t0=$(date +%s)
    c M1 -rpcclienttimeout=0 generatetoaddress 1 "$END_M" 100000000 >/dev/null
    t1=$(date +%s)
    h=$(c M1 getblockcount); b=$(c M1 getblock "$(c M1 getbestblockhash)" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d["bits"], d["nonce"])')
    echo "      bloco $h minerado em $((t1-t0)) s (bits nonce: $b)"
done
espera_altura M2 "$MAIN_BLOCKS" 60 || true
verifica "No M2 validou os blocos da rede principal" "$(c M2 getbestblockhash)" "$(c M1 getbestblockhash)"

# Bloco com prova de trabalho RandomX invalida: copia o ultimo bloco e muda so o nonce.
# Com ~131.072 hashes por bloco, o novo nonce falha o alvo com probabilidade de 99,999%.
INVALIDO=$(python3 -c '
import sys
raw = bytearray.fromhex(sys.argv[1])
n = int.from_bytes(raw[76:80], "little")
raw[76:80] = ((n + 1) % 2**32).to_bytes(4, "little")
print(raw.hex())' "$(c M1 getblock "$(c M1 getbestblockhash)" 0)")
verifica "Bloco com o nonce alterado (PoW RandomX invalida)" "$(c M2 submitblock "$INVALIDO" 2>&1 || true)" "high-hash"
verifica "Ponta do no M2 inalterada" "$(c M2 getbestblockhash)" "$(c M1 getbestblockhash)"

echo
if (( falhas == 0 )); then echo "Fase 3 validada."; else echo "$falhas verificacao(oes) falharam."; exit 1; fi
