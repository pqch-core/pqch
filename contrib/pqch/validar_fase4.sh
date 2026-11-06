#!/usr/bin/env bash
# PQCH - validacao da Fase 4: regras de rede e assinaturas pos-quanticas.
#
# Parte A (regtest, 2 nos): regras pos-quanticas ativas desde o genesis.
#   - recompensa para um endereco antigo (P2WPKH) e recusada;
#   - recompensas para P2MR; gastos assinados com ML-DSA + Schnorr (modo hibrido, o das carteiras),
#     com SLH-DSA (folha de recuperacao) e so com ML-DSA; o segundo no valida tudo pela rede P2P;
#   - assinatura pos-quantica falsificada e saida antiga recusadas pelo mempool.
# Parte B (regtest, 4 nos): limite de reorganizacao. Reorganizacao de 60 blocos aceite,
#   de 61 blocos recusada (o no fica na sua cadeia, com aviso ao operador).
#
# As transacoes sao assinadas por contrib/pqch/pq_tool.py com implementacoes independentes
# (dilithium-py, slh-dsa): PQ_PYTHON deve ter esses pacotes instalados.
#
# Uso: PQ_PYTHON=/caminho/python contrib/pqch/validar_fase4.sh [pasta_de_build]
set -euo pipefail

BUILD="${1:-build}"
BITCOIND="$BUILD/bin/pqchd"
CLI="$BUILD/bin/pqch-cli"
PY="${PQ_PYTHON:-python3}"
TOOL="$(dirname "$0")/pq_tool.py"
TMP="$(mktemp -d)"

falhas=0
verifica() { if [[ "$2" == "$3" ]]; then echo "OK    $1: $2"; else echo "FALHA $1: obtido $2, esperado $3"; falhas=$((falhas+1)); fi; }
contem() { if [[ "$2" == *"$3"* ]]; then echo "OK    $1: contem '$3'"; else echo "FALHA $1: '$2' nao contem '$3'"; falhas=$((falhas+1)); fi; }
limpar() { for d in "$TMP"/*/; do "$CLI" -datadir="$d" stop >/dev/null 2>&1 || true; done; sleep 3; rm -rf "$TMP"; }
trap limpar EXIT

# arranca um no regtest: nome, porta p2p, porta rpc, [ligar a porta]
arranca() {
    local nome=$1 p2p=$2 rpc=$3 ligar=${4:-}
    local dir="$TMP/$nome"; mkdir -p "$dir"
    {
        echo "regtest=1"; echo "server=1"; echo "rpcuser=pqch"; echo "rpcpassword=pqch"
        echo "dnsseed=0"; echo "fixedseeds=0"; echo "listenonion=0"; echo "discover=0"
        echo "[regtest]"; echo "port=$p2p"; echo "rpcport=$rpc"; echo "bind=127.0.0.1"
        [[ -n "$ligar" ]] && echo "connect=127.0.0.1:$ligar"
    } > "$dir/pqch.conf"
    "$BITCOIND" -datadir="$dir" -daemon >/dev/null
    for _ in $(seq 1 60); do "$CLI" -datadir="$dir" getblockcount >/dev/null 2>&1 && return; sleep 1; done
    echo "no $nome nao arrancou"; exit 1
}
c() { local n=$1; shift; "$CLI" -datadir="$TMP/$n" "$@"; }
js() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)"; }
espera_ponta() { # no, hash, segundos
    for _ in $(seq 1 "$3"); do [[ "$(c "$1" getbestblockhash)" == "$2" ]] && return 0; sleep 1; done; return 1; }
tool() { "$PY" "$TOOL" "$@"; }

SEED1=$(printf '11%.0s' $(seq 1 32))
SEED2=$(printf '22%.0s' $(seq 1 32))
RECOMPENSA=1250000000  # 12,5 PQCH em quanta

echo "=== Parte A: regras pos-quanticas (regtest, 2 nos) ==="
arranca A 49644 49643
arranca B 49654 49653 49644
sleep 2
verifica "Regras pos-quanticas ativas desde o bloco" "$(c A getdeploymentinfo | js 'd["deployments"]["pqrules"]["height"]')" "0"

ANTIGO="$(c A deriveaddresses "$(c A getdescriptorinfo "wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)" | js 'd["descriptor"]')" | js 'd[0]')"
erro=$(c A generatetoaddress 1 "$ANTIGO" 2>&1 || true)
contem "Recompensa para endereco antigo ($ANTIGO)" "$erro" "bad-txns-output-not-pq"

END1=$(tool address --seed "$SEED1" --kind recovery | js 'd["address"]')
END2=$(tool address --seed "$SEED2" --kind mldsa | js 'd["address"]')
verifica "Endereco P2MR (prefixo)" "${END1:0:7}" "pqcrt1p"
c A generatetoaddress 102 "$END1" >/dev/null
verifica "Altura" "$(c A getblockcount)" "102"

CB1=$(c A getblock "$(c A getblockhash 1)" | js 'd["tx"][0]')
CB2=$(c A getblock "$(c A getblockhash 2)" | js 'd["tx"][0]')

# Assinatura ML-DSA falsificada (um bit trocado): recusada.
TX=$(tool spend --seed "$SEED1" --kind recovery --txid "$CB1" --vout 0 --amount $RECOMPENSA --to "$END2" --corrupt)
erro=$(c A sendrawtransaction "$TX" 2>&1 || true)
contem "Assinatura pos-quantica falsificada" "$erro" "Invalid post-quantum signature"

# Gasto valido mas para uma saida antiga (P2WPKH): recusado por consenso.
TX=$(tool spend --seed "$SEED1" --kind recovery --txid "$CB1" --vout 0 --amount $RECOMPENSA --to "$ANTIGO")
erro=$(c A sendrawtransaction "$TX" 2>&1 || true)
contem "Pagamento para saida antiga" "$erro" "bad-txns-output-not-pq"

# Gasto hibrido (ML-DSA + Schnorr, folha A) para um endereco so ML-DSA.
TX=$(tool spend --seed "$SEED1" --kind recovery --txid "$CB1" --vout 0 --amount $RECOMPENSA --to "$END2")
TXID_H=$(c A sendrawtransaction "$TX")
INFO=$(c A getrawtransaction "$TXID_H" true)
echo "      Transacao hibrida: $(echo "$INFO" | js 'd["weight"]') WU, $(echo "$INFO" | js 'd["vsize"]') vB, testemunho de $(echo "$INFO" | js 'sum(len(x)//2 for x in d["vin"][0]["txinwitness"])') bytes"

# Gasto pela folha de recuperacao (SLH-DSA, folha B).
TX=$(tool spend --seed "$SEED1" --kind recovery --txid "$CB2" --vout 0 --amount $RECOMPENSA --to "$END2" --leaf 1)
TXID_S=$(c A sendrawtransaction "$TX")
INFO=$(c A getrawtransaction "$TXID_S" true)
echo "      Transacao SLH-DSA: $(echo "$INFO" | js 'd["weight"]') WU, $(echo "$INFO" | js 'd["vsize"]') vB"

c A generatetoaddress 1 "$END1" >/dev/null
verifica "Transacoes confirmadas no bloco 103" "$(c A getblock "$(c A getbestblockhash)" | js 'len(d["tx"])')" "3"
espera_ponta B "$(c A getbestblockhash)" 30 || true
verifica "No B na mesma ponta (validou as assinaturas)" "$(c B getbestblockhash)" "$(c A getbestblockhash)"

# Gastar a saida recebida, que so tem folha ML-DSA.
VALOR=$(c B gettxout "$TXID_H" 0 | js 'round(d["value"]*100000000)')
TX=$(tool spend --seed "$SEED2" --kind mldsa --txid "$TXID_H" --vout 0 --amount "$VALOR" --to "$END1")
TXID_M=$(c B sendrawtransaction "$TX")
for _ in $(seq 1 60); do c A getmempoolentry "$TXID_M" >/dev/null 2>&1 && break; sleep 1; done  # retransmitida de B para A
c A generatetoaddress 1 "$END1" >/dev/null
espera_ponta B "$(c A getbestblockhash)" 30 || true
verifica "Gasto ML-DSA confirmado (visto no no B)" "$(c B gettxout "$TXID_M" 0 | js 'd["confirmations"]')" "1"

for n in A B; do c $n stop >/dev/null; done
sleep 3

echo
echo "=== Parte B: limite de reorganizacao (60 blocos) ==="
# Dois pares de nos. Cada par parte de 5 blocos comuns, separa-se, mineira dois ramos e volta a ligar-se.
reorg() { # no_local, no_concorrente, porta_p2p_concorrente, blocos_locais, blocos_concorrentes
    local L=$1 R=$2 RPORT=$3 NL=$4 NR=$5
    c "$L" generatetoaddress 5 "$END1" >/dev/null
    espera_ponta "$R" "$(c "$L" getbestblockhash)" 30
    c "$L" setnetworkactive false >/dev/null; c "$R" setnetworkactive false >/dev/null
    c "$L" generatetoaddress "$NL" "$END1" >/dev/null
    c "$R" generatetoaddress "$NR" "$END2" >/dev/null
    PONTA_L=$(c "$L" getbestblockhash); PONTA_R=$(c "$R" getbestblockhash)
    c "$L" setnetworkactive true >/dev/null; c "$R" setnetworkactive true >/dev/null
    c "$L" addnode "127.0.0.1:$RPORT" onetry >/dev/null 2>&1 || true
    sleep 8
}
arranca C 49664 49663
arranca D 49674 49673 49664
sleep 2
reorg C D 49674 60 61
verifica "Reorganizacao de 60 blocos: no C segue a cadeia com mais trabalho" "$(c C getbestblockhash)" "$PONTA_R"

arranca E 49684 49683
arranca F 49694 49693 49684
sleep 2
reorg E F 49694 61 62
verifica "Reorganizacao de 61 blocos: no E recusa e fica na sua cadeia" "$(c E getbestblockhash)" "$PONTA_L"
ESTADO=$(c E getchaintips | PONTA="$PONTA_R" python3 -c 'import json,os,sys; d=json.load(sys.stdin); print([t["status"] for t in d if t["hash"]==os.environ["PONTA"]][0])')
verifica "No E conhece a outra cadeia e nao a marca invalida" "$([[ "$ESTADO" == valid-headers || "$ESTADO" == valid-fork ]] && echo "sim ($ESTADO)" || echo "nao ($ESTADO)")" "sim ($ESTADO)"
contem "Aviso ao operador" "$(c E getblockchaininfo | js 'd["warnings"]')" "refused a reorganisation of 61 blocks"

echo
if [[ $falhas -eq 0 ]]; then echo "Fase 4 validada."; else echo "Fase 4: $falhas falha(s)."; exit 1; fi
