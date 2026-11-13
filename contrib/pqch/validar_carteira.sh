#!/usr/bin/env bash
# PQCH - validacao da carteira pos-quantica (regtest, 2 nos).
#
#   - carteira nova: so descritores p2mr() com derivacao endurecida, enderecos pqcrt1p...;
#   - tipos de endereco e descritores do Bitcoin recusados (legacy, wpkh, tr);
#   - recompensas para P2MR; envio entre os dois nos, validado pela rede P2P; o gasto usa a
#     folha hibrida (ML-DSA-44 + Schnorr);
#   - depois de reiniciar o no, a carteira reconhece e gasta as moedas (cache das chaves PQ em disco);
#   - recuperacao: os descritores privados importados numa carteira vazia dao os mesmos enderecos e
#     o mesmo saldo;
#   - carteira cifrada: bloqueada nao assina, desbloqueada assina.
#
# Uso: contrib/pqch/validar_carteira.sh [pasta_de_build]
set -euo pipefail

BUILD="${1:-build}"
BITCOIND="$BUILD/bin/pqchd"
CLI="$BUILD/bin/pqch-cli"
TMP="$(mktemp -d)"

falhas=0
verifica() { if [[ "$2" == "$3" ]]; then echo "OK    $1: $2"; else echo "FALHA $1: obtido $2, esperado $3"; falhas=$((falhas+1)); fi; }
contem() { if [[ "$2" == *"$3"* ]]; then echo "OK    $1: contem '$3'"; else echo "FALHA $1: '$2' nao contem '$3'"; falhas=$((falhas+1)); fi; }
limpar() { for d in "$TMP"/*/; do "$CLI" -datadir="$d" stop >/dev/null 2>&1 || true; done; sleep 3; rm -rf "$TMP"; }
trap limpar EXIT

arranca() { # nome, porta p2p, porta rpc, [ligar a porta]
    local nome=$1 p2p=$2 rpc=$3 ligar=${4:-}
    local dir="$TMP/$nome"; mkdir -p "$dir"
    {
        echo "regtest=1"; echo "server=1"; echo "rpcuser=pqch"; echo "rpcpassword=pqch"
        echo "dnsseed=0"; echo "fixedseeds=0"; echo "listenonion=0"; echo "discover=0"; echo "fallbackfee=0.0001"
        echo "[regtest]"; echo "port=$p2p"; echo "rpcport=$rpc"; echo "bind=127.0.0.1"
        [[ -n "$ligar" ]] && echo "connect=127.0.0.1:$ligar"
    } > "$dir/pqch.conf"
    "$BITCOIND" -datadir="$dir" -daemon >/dev/null
    for _ in $(seq 1 60); do "$CLI" -datadir="$dir" getblockcount >/dev/null 2>&1 && return; sleep 1; done
    echo "no $nome nao arrancou"; exit 1
}
# O RPC fecha antes de o processo terminar; o pqchd.pid só desaparece no fim, quando a pasta fica livre.
espera_saida() { for _ in $(seq 1 120); do [[ -e "$TMP/$1/regtest/pqchd.pid" ]] || return 0; sleep 1; done; echo "no $1 nao terminou"; exit 1; }
para() { "$CLI" -datadir="$TMP/$1" stop >/dev/null; espera_saida "$1"; }
c() { local n=$1; shift; "$CLI" -datadir="$TMP/$n" "$@"; }
w() { local n=$1 wl=$2; shift 2; "$CLI" -datadir="$TMP/$n" -rpcwallet="$wl" "$@"; }
js() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)"; }
espera_mempool() { for _ in $(seq 1 30); do c "$1" getmempoolentry "$2" >/dev/null 2>&1 && return 0; sleep 1; done; return 1; }
espera_altura() { for _ in $(seq 1 30); do [[ "$(c "$1" getblockcount)" == "$2" ]] && return 0; sleep 1; done; return 1; }

arranca A 49744 49743
arranca B 49754 49753 49744
sleep 2

echo "=== Carteira nova ==="
c A createwallet a >/dev/null
c B createwallet b >/dev/null
DESCS=$(w A a listdescriptors | js '" ".join(x["desc"].split("#")[0] for x in d["descriptors"])')
verifica "Numero de descritores (recebimento + troco)" "$(w A a listdescriptors | js 'len(d["descriptors"])')" "2"
contem "Descritor de recebimento" "$DESCS" "/360h/1h/0h/0h/*h)"
contem "Descritor de troco" "$DESCS" "/360h/1h/0h/1h/*h)"
verifica "So p2mr()" "$(w A a listdescriptors | js 'all(x["desc"].startswith("p2mr(") for x in d["descriptors"])')" "True"
ADDR_A=$(w A a getnewaddress)
contem "Endereco P2MR" "$ADDR_A" "pqcrt1p"
verifica "Endereco e meu e resoluvel" "$(w A a getaddressinfo "$ADDR_A" | js 'd["ismine"] and d["solvable"]')" "True"
for t in legacy p2sh-segwit bech32; do
    contem "Tipo $t recusado" "$(w A a getnewaddress "" $t 2>&1 || true)" "No $t addresses available"
done
contem "Descritor publico so com o id da semente" "$(w A a listdescriptors | js 'd["descriptors"][0]["desc"]')" "p2mr(pqseed("
contem "Descritor privado com a semente pos-quantica" "$(w A a listdescriptors true | js 'd["descriptors"][0]["desc"]')" "p2mr(pqprv("
# Nenhuma chave de curva eliptica da derivacao no ficheiro da carteira (registos de chaves e xpub).
w A a backupwallet "$TMP/a.bak" >/dev/null
TIPOS=$(python3 - "$TMP/a.bak" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
tipos = set()
for (k,) in db.execute("select key from main"):
    n = k[0]; tipos.add(k[1:1+n].decode())
print(" ".join(sorted(tipos)))
PY
)
for t in walletdescriptorkey walletdescriptorckey walletdescriptorcache walletdescriptorlhcache; do
    verifica "Sem registos $t no ficheiro da carteira" "$([[ " $TIPOS " == *" $t "* ]] && echo sim || echo nao)" "nao"
done
contem "Semente pos-quantica guardada" "$TIPOS" "walletdescriptorpqseed"
verifica "Sem tpub no ficheiro da carteira" "$(grep -ac tpub "$TMP/a.bak" || true)" "0"
# Uma tprv qualquer, so para testar que os descritores do Bitcoin sao recusados.
TPRV=$(python3 - <<'PY'
import hashlib, hmac
I = hmac.new(b"Bitcoin seed", bytes(32), hashlib.sha512).digest()
raw = bytes.fromhex("04358394") + bytes(9) + I[32:] + b"\x00" + I[:32]
raw += hashlib.sha256(hashlib.sha256(raw).digest()).digest()[:4]
a = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
n = int.from_bytes(raw, "big"); out = ""
while n: n, r = divmod(n, 58); out = a[r] + out
print(out)
PY
)
TR="tr($TPRV/86h/1h/0h/0/*)"
TR="$TR#$(c A getdescriptorinfo "$TR" | js 'd["checksum"]')"
R=$(w A a importdescriptors "[{\"desc\":\"$TR\",\"timestamp\":\"now\",\"active\":false}]" 2>&1 || true)
contem "Descritor tr() recusado" "$R" "Only p2mr(), p2mr_vault(), addr() and raw() descriptors"
WPKH="wpkh($TPRV/84h/1h/0h/0/*)"
WPKH="$WPKH#$(c A getdescriptorinfo "$WPKH" | js 'd["checksum"]')"
R=$(w A a importdescriptors "[{\"desc\":\"$WPKH\",\"timestamp\":\"now\",\"active\":false}]" 2>&1 || true)
contem "Descritor wpkh() recusado" "$R" "Only p2mr(), p2mr_vault(), addr() and raw() descriptors"
arranca C 49764 49763
para C
echo "addresstype=bech32" >> "$TMP/C/pqch.conf"
"$BITCOIND" -datadir="$TMP/C" -daemon >/dev/null
for _ in $(seq 1 60); do c C getblockcount >/dev/null 2>&1 && break; sleep 1; done
contem "-addresstype=bech32 recusado" "$(c C createwallet c 2>&1 || true)" "only bech32m"
para C

contem "createwalletdescriptor bech32 recusado" "$(w A a createwalletdescriptor bech32 2>&1 || true)" "only bech32m"

echo "=== Minerar e enviar ==="
c A generatetoaddress 101 "$ADDR_A" >/dev/null
espera_altura B 101 || true
verifica "Saldo disponivel em A (1 recompensa madura)" "$(w A a getbalance)" "12.50000000"
ADDR_B=$(w B b getnewaddress)
OLD=$(c A deriveaddresses "$(c A getdescriptorinfo "wpkh($TPRV/0/0)" | js 'd["descriptor"]')" | js 'd[0]')
contem "Envio para endereco antigo ($OLD) recusado" "$(w A a sendtoaddress "$OLD" 1 2>&1 || true)" "not valid on this network"
HEX=$(w A a send "{\"$ADDR_A\": 1}" null "unset" null "{\"change_type\": \"bech32\", \"add_to_wallet\": false}" | js 'd["hex"]')
verifica "Troco pedido como bech32 sai P2MR" "$(c A decoderawtransaction "$HEX" | js 'len(d["vout"])==2 and all(o["scriptPubKey"]["address"].startswith("pqcrt1p") for o in d["vout"])')" "True"
TX=$(w A a sendtoaddress "$ADDR_B" 5)
espera_mempool B "$TX" && verifica "Transacao chegou ao no B pela rede" "sim" "sim" || verifica "Transacao chegou ao no B pela rede" "nao" "sim"
WIT=$(c A getrawtransaction "$TX" 1 | js '[len(x)//2 for x in d["vin"][0]["txinwitness"]]')
verifica "Testemunho da folha hibrida (ML-DSA, Schnorr, script, controlo)" "$WIT" "[2420, 64, 1350, 33]"
verifica "Saidas P2MR" "$(c A getrawtransaction "$TX" 1 | js 'all(o["scriptPubKey"]["address"].startswith("pqcrt1p") for o in d["vout"])')" "True"
c A generatetoaddress 1 "$ADDR_A" >/dev/null
espera_altura B 102
verifica "B recebeu 5 PQCH confirmados" "$(w B b getbalance)" "5.00000000"

echo "=== Reiniciar e gastar outra vez ==="
para B
arranca B 49754 49753 49744
c B loadwallet b >/dev/null
verifica "Saldo de B depois de reiniciar" "$(w B b getbalance)" "5.00000000"
TX2=$(w B b sendtoaddress "$ADDR_A" 2)
espera_mempool A "$TX2" && verifica "Gasto de B depois de reiniciar aceite por A" "sim" "sim" || verifica "Gasto de B depois de reiniciar aceite por A" "nao" "sim"
c A generatetoaddress 1 "$ADDR_A" >/dev/null
espera_altura B 103

echo "=== Recuperacao a partir dos descritores privados ==="
PRIV=$(w B b listdescriptors true | js '__import__("json").dumps([{"desc": x["desc"], "timestamp": 0, "active": True, "internal": x["internal"], "range": x["range"]} for x in d["descriptors"]])')
c B createwallet r2 false true >/dev/null
R=$(w B r2 importdescriptors "$PRIV")
verifica "Importacao dos descritores" "$(echo "$R" | js 'all(x["success"] for x in d)')" "True"
verifica "Saldo recuperado igual ao original" "$(w B r2 getbalance)" "$(w B b getbalance)"
verifica "Primeiro endereco recuperado igual" "$(w B r2 getaddressinfo "$ADDR_B" | js 'd["ismine"]')" "True"

echo "=== Carteira cifrada ==="
w B r2 encryptwallet "frase-secreta" >/dev/null
ADDR_C=$(w A a getnewaddress)
contem "Bloqueada nao assina" "$(w B r2 sendtoaddress "$ADDR_C" 1 2>&1 || true)" "walletpassphrase"
w B r2 walletpassphrase "frase-secreta" 60
TX3=$(w B r2 sendtoaddress "$ADDR_C" 1)
espera_mempool A "$TX3" && verifica "Desbloqueada assina (aceite por A)" "sim" "sim" || verifica "Desbloqueada assina (aceite por A)" "nao" "sim"

w B r2 backupwallet "$TMP/r2.bak" >/dev/null
TIPOS2=$(python3 - "$TMP/r2.bak" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
print(" ".join(sorted({k[1:1+k[0]].decode() for (k,) in db.execute("select key from main")})))
PY
)
contem "Semente cifrada no ficheiro" "$TIPOS2" "walletdescriptorcpqseed"
verifica "Sem semente em claro no ficheiro" "$([[ " $TIPOS2 " == *" walletdescriptorpqseed "* ]] && echo sim || echo nao)" "nao"
para B
arranca B 49754 49753 49744
c B loadwallet r2 >/dev/null
w B r2 walletpassphrase "frase-secreta" 60
TX4=$(w B r2 sendtoaddress "$ADDR_C" 0.5)
espera_mempool A "$TX4" && verifica "Carteira cifrada assina depois de reiniciar" "sim" "sim" || verifica "Carteira cifrada assina depois de reiniciar" "nao" "sim"
contem "Frase errada recusada" "$(w B r2 walletpassphrase "errada" 60 2>&1 || true)" "passphrase entered was incorrect"

echo
if [[ $falhas -eq 0 ]]; then echo "Todas as verificacoes passaram."; else echo "$falhas verificacao(oes) falharam."; exit 1; fi
