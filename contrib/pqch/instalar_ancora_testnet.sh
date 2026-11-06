#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
#
# Prepara um servidor Ubuntu 24.04 limpo como nó âncora da testnet fechada (ver TESTNET.md).
#
#   sudo contrib/pqch/instalar_ancora_testnet.sh
#
# Corre dentro de um clone do repositório. Por esta ordem:
#   1. atualiza o sistema, liga as atualizações de segurança automáticas e a hora por NTP;
#   2. cria o utilizador "pqch", sem sudo e sem shell, dono do nó e dos dados;
#   3. compila o PQCH (só o nó, a CLI e a ferramenta de carteira) e instala-o em /usr/local/bin;
#   4. monta o centro da VPN da equipa (WireGuard, interface "pqch", 10.77.0.1/24, UDP 51820);
#   5. ativa a firewall: entra só SSH e a VPN; a porta 39333 da testnet só pela VPN;
#   6. escreve /home/pqch/.pqch/pqch.conf (escuta só na VPN, sem sementes, sem anunciar o IP);
#   7. instala o serviço systemd "pqchd-testnet", que arranca com a máquina depois da VPN.
# Pode correr outra vez (para atualizar o PQCH depois de um git pull): não toca nas chaves da VPN
# nem no pqch.conf se já existirem.
#
# Membros da VPN: contrib/pqch/adicionar_membro_vpn.sh

set -euo pipefail

VPN_IF="pqch"
VPN_NET="10.77.0"
VPN_ADDR="${VPN_NET}.1"
VPN_PORT=51820
P2P_PORT=39333
NODE_USER="pqch"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

if [[ $EUID -ne 0 ]]; then
    echo "Corre com sudo: sudo $0" >&2
    exit 1
fi
[[ -f "${REPO_DIR}/src/init.cpp" ]] || { echo "Corre a partir de um clone do repositório PQCH." >&2; exit 1; }
. /etc/os-release
[[ "${ID}" == "ubuntu" ]] || echo "AVISO: preparado para Ubuntu 24.04; este sistema é ${PRETTY_NAME}."

echo "== 1/7 Sistema"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get -y -q upgrade
apt-get install -y -q --no-install-recommends \
    build-essential cmake pkgconf python3 libevent-dev libboost-dev libsqlite3-dev ccache git \
    wireguard ufw unattended-upgrades ca-certificates
dpkg-reconfigure -f noninteractive unattended-upgrades
timedatectl set-ntp true || true

echo "== 2/7 Utilizador ${NODE_USER}"
if ! id "${NODE_USER}" >/dev/null 2>&1; then
    useradd --create-home --shell /usr/sbin/nologin "${NODE_USER}"
fi
NODE_HOME="$(getent passwd "${NODE_USER}" | cut -d: -f6)"
install -d -m 0750 -o "${NODE_USER}" -g "${NODE_USER}" "${NODE_HOME}/.pqch"

echo "== 3/7 Compilar o PQCH ($(nproc) núcleos; a primeira vez demora)"
cmake -S "${REPO_DIR}" -B "${REPO_DIR}/build-ancora" -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_WALLET=ON -DBUILD_GUI=OFF -DENABLE_IPC=OFF -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DWITH_CCACHE=ON >/dev/null
cmake --build "${REPO_DIR}/build-ancora" -j"$(nproc)"
cmake --install "${REPO_DIR}/build-ancora" --prefix /usr/local >/dev/null
/usr/local/bin/pqchd -version | head -1

echo "== 4/7 VPN (WireGuard, ${VPN_ADDR}/24, UDP ${VPN_PORT})"
install -d -m 0700 /etc/wireguard
if [[ ! -f "/etc/wireguard/${VPN_IF}.key" ]]; then
    (umask 077; wg genkey > "/etc/wireguard/${VPN_IF}.key")
fi
wg pubkey < "/etc/wireguard/${VPN_IF}.key" > "/etc/wireguard/${VPN_IF}.pub"
if [[ ! -f "/etc/wireguard/${VPN_IF}.conf" ]]; then
    (umask 077; cat > "/etc/wireguard/${VPN_IF}.conf" <<EOF
# Centro da VPN da testnet PQCH. Membros: contrib/pqch/adicionar_membro_vpn.sh
[Interface]
Address = ${VPN_ADDR}/24
ListenPort = ${VPN_PORT}
PostUp = wg set %i private-key /etc/wireguard/${VPN_IF}.key
EOF
    )
fi
# Os membros falam entre si através deste servidor.
echo "net.ipv4.ip_forward=1" > /etc/sysctl.d/90-pqch-vpn.conf
sysctl -q --system
systemctl enable --now "wg-quick@${VPN_IF}"

echo "== 5/7 Firewall"
ufw default deny incoming >/dev/null
ufw default allow outgoing >/dev/null
ufw allow OpenSSH >/dev/null
ufw allow "${VPN_PORT}/udp" comment 'WireGuard PQCH' >/dev/null
ufw allow in on "${VPN_IF}" to any port "${P2P_PORT}" proto tcp comment 'PQCH testnet, so pela VPN' >/dev/null
ufw route allow in on "${VPN_IF}" out on "${VPN_IF}" >/dev/null
ufw --force enable >/dev/null
ufw status verbose | sed -n '1,20p'

echo "== 6/7 pqch.conf"
CONF="${NODE_HOME}/.pqch/pqch.conf"
if [[ ! -f "${CONF}" ]]; then
    cat > "${CONF}" <<EOF
# Nó âncora da testnet fechada (TESTNET.md)
testnet=1
server=1
[test]
bind=${VPN_ADDR}
dnsseed=0
fixedseeds=0
discover=0
natpmp=0
listenonion=0
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
# Membros sempre ligados (opcional): addnode=${VPN_NET}.2
EOF
    chown "${NODE_USER}:${NODE_USER}" "${CONF}"
    chmod 0640 "${CONF}"
fi

echo "== 7/7 Serviço pqchd-testnet"
cat > /etc/systemd/system/pqchd-testnet.service <<EOF
[Unit]
Description=PQCH testnet (nó âncora)
After=network-online.target wg-quick@${VPN_IF}.service
Wants=network-online.target
Requires=wg-quick@${VPN_IF}.service

[Service]
User=${NODE_USER}
Group=${NODE_USER}
ExecStart=/usr/local/bin/pqchd -conf=${CONF} -datadir=${NODE_HOME}/.pqch
Type=exec
Restart=on-failure
TimeoutStartSec=infinity
TimeoutStopSec=600
# Proteções (como contrib/init/bitcoind.service). Sem MemoryDenyWriteExecute: o RandomX compila
# código em tempo real e precisa de memória executável.
PrivateTmp=true
ProtectSystem=full
ProtectHome=read-only
ReadWritePaths=${NODE_HOME}/.pqch
NoNewPrivileges=true
PrivateDevices=true

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable pqchd-testnet >/dev/null
systemctl restart pqchd-testnet
sleep 5
systemctl --no-pager status pqchd-testnet | head -5

cat <<EOF

Feito.
  Chave pública da VPN deste servidor: $(cat "/etc/wireguard/${VPN_IF}.pub")
  Endereço público para os membros:    $(curl -4 -fsS https://ifconfig.me 2>/dev/null || echo '<IP público>'):${VPN_PORT}

Comandos úteis:
  sudo -u ${NODE_USER} -H pqch-cli -testnet getblockchaininfo
  sudo -u ${NODE_USER} -H pqch-cli -testnet getpeerinfo
  journalctl -u pqchd-testnet -f
  sudo wg show
Adicionar um membro: sudo contrib/pqch/adicionar_membro_vpn.sh <nome> <n.º 2-254> [chave pública]
EOF
