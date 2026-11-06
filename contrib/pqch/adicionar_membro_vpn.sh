#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
#
# Acrescenta um membro da equipa à VPN da testnet, no nó âncora (TESTNET.md).
#
#   sudo contrib/pqch/adicionar_membro_vpn.sh <nome> <n.º 2-254> [chave pública do membro]
#
# O melhor é o membro gerar as chaves no seu computador (wg genkey | tee privada | wg pubkey) e
# mandar só a pública. Sem chave pública, o script gera o par e mostra a configuração completa do
# membro, com a chave privada: entrega-a por um canal seguro e não a guardes.

set -euo pipefail

VPN_IF="pqch"
VPN_NET="10.77.0"
VPN_PORT=51820

[[ $EUID -eq 0 ]] || { echo "Corre com sudo." >&2; exit 1; }
NAME="${1:-}"; NUM="${2:-}"; PUB="${3:-}"
[[ "${NAME}" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "Uso: $0 <nome> <n.º 2-254> [chave pública]" >&2; exit 1; }
[[ "${NUM}" =~ ^[0-9]+$ ]] && (( NUM >= 2 && NUM <= 254 )) || { echo "O número vai de 2 a 254 (o 1 é este servidor)." >&2; exit 1; }
ADDR="${VPN_NET}.${NUM}"
CONF="/etc/wireguard/${VPN_IF}.conf"
if grep -q "AllowedIPs = ${ADDR}/32" "${CONF}"; then
    echo "O endereço ${ADDR} já está atribuído:" >&2
    grep -B2 "AllowedIPs = ${ADDR}/32" "${CONF}" >&2
    exit 1
fi

PRIV=""
if [[ -z "${PUB}" ]]; then
    PRIV="$(wg genkey)"
    PUB="$(echo "${PRIV}" | wg pubkey)"
fi
[[ "${PUB}" =~ ^[A-Za-z0-9+/]{43}=$ ]] || { echo "Chave pública inválida." >&2; exit 1; }

cat >> "${CONF}" <<EOF

[Peer]
# ${NAME}
PublicKey = ${PUB}
AllowedIPs = ${ADDR}/32
EOF
wg set "${VPN_IF}" peer "${PUB}" allowed-ips "${ADDR}/32"
echo "Membro ${NAME} acrescentado com o endereço ${ADDR}."

SERVER_PUB="$(cat "/etc/wireguard/${VPN_IF}.pub")"
SERVER_IP="$(curl -4 -fsS https://ifconfig.me 2>/dev/null || echo '<IP público do servidor>')"
cat <<EOF

--- Configuração WireGuard do membro (ficheiro pqch.conf no WireGuard dele) ---
[Interface]
Address = ${ADDR}/24
PrivateKey = ${PRIV:-<a chave privada que o membro gerou>}

[Peer]
PublicKey = ${SERVER_PUB}
Endpoint = ${SERVER_IP}:${VPN_PORT}
AllowedIPs = ${VPN_NET}.0/24
PersistentKeepalive = 25

--- pqch.conf do membro ---
testnet=1
[test]
bind=${ADDR}
addnode=${VPN_NET}.1
dnsseed=0
fixedseeds=0
discover=0
natpmp=0
listenonion=0
EOF
[[ -n "${PRIV}" ]] && echo $'\nATENÇÃO: a configuração acima tem a chave privada do membro. Entrega-a por um canal seguro e não a guardes noutro sítio.'
exit 0
