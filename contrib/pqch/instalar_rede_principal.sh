#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Prepara o nó da rede principal num dos nós públicos (âncora ou VPS), sem o arrancar.
#
#   sudo contrib/pqch/instalar_rede_principal.sh preparar <IP público> [quota de CPU, ex.: 50%]
#   sudo contrib/pqch/instalar_rede_principal.sh estado
#   sudo contrib/pqch/instalar_rede_principal.sh arrancar      # só no dia 27, com o pqchd 1.0.0
#
# O que "preparar" faz (pode correr outra vez; não mexe no nó da testnet):
#   1. copia para /root/copia-rede-principal-<data>/ as regras da firewall e a lista de serviços;
#   2. cria /var/lib/pqch-main (utilizador pqch, 0750) e o pqch.conf da rede principal:
#      escuta na 29333, RPC só em 127.0.0.1, sem carteira, sementes fixas do código, sem DNS;
#   3. escreve o serviço pqchd-main, DESLIGADO: não arranca no boot nem agora. Mesmo que alguém o
#      arranque à mão, recusa correr com um pqchd anterior ao 1.0.0 (génesis provisório);
#   4. abre a porta 29333/tcp na ufw (a RPC 29332 continua fechada).
set -euo pipefail

NODE_USER=pqch
DATADIR=/var/lib/pqch-main
CONF=${DATADIR}/pqch.conf
UNIT=/etc/systemd/system/pqchd-main.service
P2P_PORT=29333

falha() { echo "ERRO: $*" >&2; exit 1; }
msg() { echo; echo "== $*"; }
[[ $EUID -eq 0 ]] || falha "corre com sudo."
id "${NODE_USER}" >/dev/null 2>&1 || falha "o utilizador ${NODE_USER} não existe (este script é para a âncora e a VPS)."
[[ -x /usr/local/bin/pqchd ]] || falha "não encontro /usr/local/bin/pqchd."

versao_pqchd() { /usr/local/bin/pqchd -version | head -1 | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' || true; }

cmd_preparar() {
    local ip="${1:-}" quota="${2:-100%}"
    [[ "${ip}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || falha "indica o IP público desta máquina (ex.: 72.61.20.80)."
    [[ "${quota}" =~ ^[0-9]+%$ ]] || falha "a quota de CPU tem a forma 50%."
    if ! ip -4 addr | grep -qw "${ip}"; then
        echo "AVISO: ${ip} não aparece nas interfaces desta máquina (normal atrás de NAT; confirma que é o IP certo)."
    fi

    msg "1/4 Cópia de segurança da configuração"
    local copia="/root/copia-rede-principal-$(date +%Y%m%d-%H%M%S)"
    install -d -m 0700 "${copia}"
    cp -a /etc/ufw "${copia}/ufw" 2>/dev/null || true
    ufw status numbered > "${copia}/ufw-status.txt" 2>&1 || true
    systemctl list-unit-files 'pqch*' 'wg-quick*' 'nginx*' --no-pager > "${copia}/servicos.txt" 2>&1 || true
    [[ -f "${UNIT}" ]] && cp -a "${UNIT}" "${copia}/"
    [[ -f "${CONF}" ]] && cp -a "${CONF}" "${copia}/"
    echo "Guardado em ${copia}"

    msg "2/4 Pasta de dados e pqch.conf (${CONF})"
    install -d -m 0750 -o "${NODE_USER}" -g "${NODE_USER}" "${DATADIR}"
    cat > "${CONF}.novo" <<EOF
# PQCH, rede principal: nó público do projeto (contrib/pqch/instalar_rede_principal.sh)
server=1
listen=1
port=${P2P_PORT}
externalip=${ip}
maxconnections=40
# Sementes: as fixas do código (os dois nós públicos). Sem sementes de DNS no lançamento.
fixedseeds=1
dnsseed=0
discover=0
natpmp=0
listenonion=0
# Sem carteira neste nó.
disablewallet=1
dbcache=300
maxmempool=100
# RPC só dentro da máquina, nunca exposta.
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
EOF
    if [[ -f "${CONF}" ]] && ! cmp -s "${CONF}" "${CONF}.novo"; then
        echo "O pqch.conf já existia e é diferente: a versão anterior ficou na cópia de segurança."
    fi
    mv "${CONF}.novo" "${CONF}"
    chown "${NODE_USER}:${NODE_USER}" "${CONF}"
    chmod 0640 "${CONF}"

    msg "3/4 Serviço pqchd-main (desligado)"
    cat > "${UNIT}" <<EOF
[Unit]
Description=PQCH rede principal (nó público)
After=network-online.target
Wants=network-online.target

[Service]
User=${NODE_USER}
Group=${NODE_USER}
# Recusa arrancar com um pqchd anterior ao 1.0.0: esses têm o génesis provisório.
ExecStartPre=/bin/sh -c '/usr/local/bin/pqchd -version | head -1 | grep -qE "v([1-9][0-9]*)\\\\.[0-9]+\\\\.[0-9]+"'
ExecStart=/usr/local/bin/pqchd -conf=${CONF} -datadir=${DATADIR}
Type=exec
Restart=on-failure
TimeoutStartSec=infinity
TimeoutStopSec=600
# Limites: o resto da máquina (site, testnet) mantém prioridade.
CPUQuota=${quota}
MemoryHigh=2G
MemoryMax=2560M
Nice=10
IOWeight=50
# Proteções (como contrib/init/bitcoind.service). Sem MemoryDenyWriteExecute: o RandomX compila
# código em tempo real.
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
ReadWritePaths=${DATADIR}
NoNewPrivileges=true
PrivateDevices=true

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl disable pqchd-main >/dev/null 2>&1 || true
    echo "Escrito ${UNIT}; o serviço fica desligado até ao dia do lançamento."

    msg "4/4 Firewall: porta ${P2P_PORT}/tcp"
    if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
        ufw allow "${P2P_PORT}/tcp" comment 'PQCH rede principal' >/dev/null
        ufw status | grep -E "${P2P_PORT}|29332" || true
    else
        echo "AVISO: a ufw não está ativa nesta máquina; não mexi na firewall."
    fi
    echo
    echo "Feito. Falta, fora da máquina: abrir a ${P2P_PORT}/tcp na firewall do painel da Hostinger, se houver uma."
    cmd_estado
}

cmd_estado() {
    msg "Estado"
    echo "pqchd instalado: $(versao_pqchd)"
    echo "pqch.conf: $([[ -f "${CONF}" ]] && echo "${CONF}" || echo 'não existe')"
    local ativo ligado
    ligado=$(systemctl is-enabled pqchd-main 2>/dev/null) || true
    ativo=$(systemctl is-active pqchd-main 2>/dev/null) || true
    echo "Serviço pqchd-main: ${ligado:-não existe} / ${ativo:-inactive}"
    echo "Serviço da testnet: $(systemctl is-active pqchd-testnet 2>/dev/null || echo 'não existe')"
    echo "Portas à escuta (29332/29333/39332/39333):"
    ss -ltnH 2>/dev/null | awk '{print $4}' | grep -E ':(29332|29333|39332|39333)$' || echo "  nenhuma"
    if ufw status 2>/dev/null | grep -q 29332; then echo "AVISO: a RPC 29332 aparece na ufw: não deve estar aberta."; fi
    df -h "${DATADIR%/*}" | tail -1 | awk '{print "Disco livre em " $6 ": " $4}'
}

cmd_arrancar() {
    local v; v=$(versao_pqchd)
    [[ "${v}" =~ ^v[1-9] ]] || falha "o pqchd instalado é ${v:-desconhecido}: instala primeiro a 1.0.0 assinada."
    [[ -f "${CONF}" ]] || falha "corre primeiro: preparar <IP público>."
    systemctl enable --now pqchd-main
    sleep 5
    systemctl --no-pager status pqchd-main | head -5
    sudo -u "${NODE_USER}" /usr/local/bin/pqch-cli -datadir="${DATADIR}" -conf="${CONF}" getblockchaininfo | head -8
}

case "${1:-}" in
    preparar) shift; cmd_preparar "$@" ;;
    estado) cmd_estado ;;
    arrancar) cmd_arrancar ;;
    *) sed -n '5,10p' "$0"; exit 1 ;;
esac
