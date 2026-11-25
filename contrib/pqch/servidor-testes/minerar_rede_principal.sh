#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Passa uma máquina de casa da equipa (o server, o Kali) da mineração da testnet para a mineração da
# rede principal, pelo pool PQCH, no lançamento de 27 nov. 2026 às 23:00 UTC.
#
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh preparar --pool <pasta do pqch-pool> --threads N [--fast]
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh estado
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh binarios 1.0.0     # sexta, depois da release assinada
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh arrancar-no        # sexta, 22:30
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh arrancar --morada pqc1p...   # sexta, 23:00
#   sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh parar             # volta a minerar a testnet
#
# Regras (LANCAMENTO.md):
#   - Sem carteira com valor nesta máquina: o nó da rede principal tem disablewallet=1 e as recompensas
#     vão para uma morada (--morada) de uma carteira que está fora das máquinas de teste.
#   - Ninguém mina antes das 23:00 UTC de 27 nov.: "arrancar" recusa antes dessa hora.
#   - Sem portas abertas em casa: o nó e o pool só fazem ligações de saída (listen=0; nada no router).
#   - A RPC fica só em 127.0.0.1.
#
# Nó da rede principal: se a máquina já tem o nó do explorador na rede principal (pqchd.service,
# /srv/pqch), o pool usa esse nó. Senão, "preparar" cria um nó só para minerar (pqchd-main,
# utilizador pqch-minerador, /var/lib/pqch-main), DESLIGADO até sexta.
#
# "preparar" não arranca nem para nada: pode correr já, com o pqchd 0.3.x instalado.
set -euo pipefail

CFG=/etc/pqch-minerar-main.conf
LAUNCH_EPOCH=1795820400          # 2026-11-27T23:00:00Z
FPR="1CDFA6A84A35AA6D7B524D1CFB6E21DF0D38B5DA"
REPO_URL="https://github.com/pqch-core/pqch"
ANCORA_POOL="187.127.89.83:29340"
TESTNET_MINERS=(pqch-pool-testnet pqch-minerar-testnet)

falha() { echo "ERRO: $*" >&2; exit 1; }
msg() { echo; echo "== $*"; }
[[ $EUID -eq 0 ]] || falha "corre com sudo."

versao_pqchd() { /usr/local/bin/pqchd -version 2>/dev/null | head -1 | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' || true; }
carregar() { [[ -f "${CFG}" ]] || falha "corre primeiro: preparar"; source "${CFG}"; }
cli() { sudo -u "${NODE_USER}" /usr/local/bin/pqch-cli -datadir="${DATADIR}" -conf="${DATADIR}/pqch.conf" "$@"; }

no_do_explorador() {
    # O nó do explorador (pqch-explorer/deploy/instalar_servidor.sh) na rede principal.
    [[ -f /srv/pqch/pqch.conf ]] && ! grep -q '^testnet=1' /srv/pqch/pqch.conf \
        && systemctl cat pqchd.service 2>/dev/null | grep -q 'PQCH Explorer'
}

cmd_preparar() {
    local pool="" threads="" fast=0
    while (( $# )); do
        case "$1" in
            --pool) pool=$2; shift 2 ;;
            --threads) threads=$2; shift 2 ;;
            --fast) fast=1; shift ;;
            *) falha "opção desconhecida: $1" ;;
        esac
    done
    [[ -n "${pool}" && -x "${pool}/deploy/instalar_pool_testnet.sh" ]] || falha "indica --pool <pasta do clone do pqch-pool>."
    pool="$(cd "${pool}" && pwd)"
    grep -q -- '--rede main' "${pool}/deploy/instalar_pool_testnet.sh" || falha "o pqch-pool em ${pool} não tem --rede main: faz git pull."
    [[ "${threads}" =~ ^[1-9][0-9]*$ ]] && (( threads <= $(nproc) )) || falha "--threads entre 1 e $(nproc)."
    if (( fast )); then
        local mem; mem=$(awk '/MemTotal/ {print int($2/1048576)}' /proc/meminfo)
        (( mem >= 6 )) || falha "--fast precisa de ~2,1 GB só para o RandomX; esta máquina tem ${mem} GB no total."
    fi

    msg "1/5 Cópia de segurança da configuração"
    local copia="/root/copia-minerar-main-$(date +%Y%m%d-%H%M%S)"
    install -d -m 0700 "${copia}"
    systemctl list-unit-files 'pqch*' 'p2pool*' 'xmrig*' 'monerod*' --no-pager > "${copia}/servicos.txt" 2>&1 || true
    systemctl list-units 'pqch*' 'p2pool*' 'xmrig*' --no-pager > "${copia}/ativos.txt" 2>&1 || true
    [[ -f "${CFG}" ]] && cp -a "${CFG}" "${copia}/"
    [[ -f /etc/systemd/system/pqchd-main.service ]] && cp -a /etc/systemd/system/pqchd-main.service "${copia}/"
    echo "Guardado em ${copia}"

    msg "2/5 Nó da rede principal"
    local node_user datadir node_svc proprio
    if no_do_explorador; then
        node_user=pqch; datadir=/srv/pqch; node_svc=pqchd; proprio=0
        echo "Esta máquina tem o nó do explorador na rede principal (/srv/pqch): o pool usa esse nó."
        echo "Não crio outro nó (seriam duas cadeias e as mesmas portas)."
    else
        node_user=pqch-minerador; datadir=/var/lib/pqch-main; node_svc=pqchd-main; proprio=1
        id "${node_user}" >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin "${node_user}"
        install -d -m 0750 -o "${node_user}" -g "${node_user}" "${datadir}"
        cat > "${datadir}/pqch.conf" <<EOF
# PQCH, rede principal: nó de mineração da equipa, em casa (minerar_rede_principal.sh)
server=1
# Só ligações de saída: nenhuma porta aberta nem reencaminhada no router.
listen=0
discover=0
natpmp=0
listenonion=0
fixedseeds=1
dnsseed=0
addnode=187.127.89.83:29333
addnode=72.61.20.80:29333
# Sem carteira: as recompensas vão para uma morada de fora (--morada no pool).
disablewallet=1
dbcache=300
maxmempool=100
# RPC só dentro da máquina, nunca exposta.
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
EOF
        chown "${node_user}:${node_user}" "${datadir}/pqch.conf"; chmod 0640 "${datadir}/pqch.conf"
        cat > /etc/systemd/system/pqchd-main.service <<EOF
[Unit]
Description=PQCH rede principal (nó de mineração da equipa)
After=network-online.target
Wants=network-online.target

[Service]
User=${node_user}
Group=${node_user}
# Recusa arrancar com um pqchd anterior ao 1.0.0: esses têm o génesis provisório.
ExecStartPre=/bin/sh -c '/usr/local/bin/pqchd -version | head -1 | grep -qE "v([1-9][0-9]*)\\\\.[0-9]+\\\\.[0-9]+"'
ExecStart=/usr/local/bin/pqchd -conf=${datadir}/pqch.conf -datadir=${datadir}
Type=exec
Restart=on-failure
TimeoutStartSec=infinity
TimeoutStopSec=600
MemoryHigh=2G
MemoryMax=2560M
Nice=5
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
ReadWritePaths=${datadir}
NoNewPrivileges=true
PrivateDevices=true

[Install]
WantedBy=multi-user.target
EOF
        systemctl daemon-reload
        systemctl disable pqchd-main >/dev/null 2>&1 || true
        echo "Nó pqchd-main escrito em ${datadir}, DESLIGADO até sexta."
    fi

    msg "3/5 Definições guardadas em ${CFG}"
    cat > "${CFG}" <<EOF
# minerar_rede_principal.sh (preparado a $(date -u +%Y-%m-%dT%H:%M:%SZ))
NODE_USER=${node_user}
DATADIR=${datadir}
NODE_SVC=${node_svc}
PROPRIO=${proprio}
POOL_REPO=${pool}
THREADS=${threads}
FAST=${fast}
EOF
    chmod 0644 "${CFG}"
    cat "${CFG}"

    msg "4/5 Compilar já o pqch-pool (sexta às 23:00 só reinstala o serviço)"
    "${pool}/deploy/compilar.sh"
    echo "(o pool da testnet que está a correr continua com o programa que já tinha em memória)"

    msg "5/5 Mineração da testnet nesta máquina (continua até sexta às 23:00)"
    local s
    for s in "${TESTNET_MINERS[@]}" p2pool xmrig; do
        systemctl list-unit-files "${s}.service" --no-legend 2>/dev/null | grep -q . \
            && echo "  ${s}: $(systemctl is-active "${s}" 2>/dev/null || true)"
    done
    echo
    echo "Preparado. Nada foi arrancado nem parado."
    cmd_estado
}

cmd_binarios() {
    local v="${1:-}"
    [[ "${v}" =~ ^[1-9][0-9]*\.[0-9]+\.[0-9]+$ ]] || falha "indica a versão, ex.: binarios 1.0.0"
    local tmp tar; tmp="$(mktemp -d)"; trap 'rm -rf "${tmp}"' RETURN
    tar="pqch-${v}-x86_64-linux-gnu.tar.gz"
    msg "PQCH ${v}: descarregar e conferir a assinatura"
    for f in "${tar}" SHA256SUMS SHA256SUMS.asc pqch-releases.asc; do
        curl -fsSL -o "${tmp}/${f}" "${REPO_URL}/releases/download/v${v}/${f}"
    done
    export GNUPGHOME="${tmp}/gpg"; install -d -m 700 "${GNUPGHOME}"
    gpg -q --import "${tmp}/pqch-releases.asc"
    gpg --status-fd 1 --verify "${tmp}/SHA256SUMS.asc" "${tmp}/SHA256SUMS" 2>/dev/null | grep -q "VALIDSIG ${FPR}" \
        || falha "a assinatura do SHA256SUMS não é da chave ${FPR}."
    ( cd "${tmp}" && grep " ${tar}\$" SHA256SUMS | sha256sum -c - ) || falha "o ficheiro não confere com o SHA256SUMS."
    unset GNUPGHOME
    tar -xzf "${tmp}/${tar}" -C "${tmp}"
    install -m 0755 "${tmp}/pqch-${v}/bin/pqchd" "${tmp}/pqch-${v}/bin/pqch-cli" /usr/local/bin/
    echo "Instalado: $(versao_pqchd)"
    echo "O nó da testnet desta máquina passa a usar esta versão quando reiniciar (systemctl restart pqchd-testnet)."
}

cmd_arrancar_no() {
    carregar
    local v; v=$(versao_pqchd)
    [[ "${v}" =~ ^v[1-9] ]] || falha "o pqchd instalado é ${v:-desconhecido}: instala primeiro a 1.0.0 assinada (binarios 1.0.0)."
    if (( PROPRIO )); then
        systemctl enable --now pqchd-main
    else
        systemctl is-active --quiet pqchd || falha "o nó do explorador (pqchd) não está a correr: segue primeiro o passo 8 do LANCAMENTO.md."
    fi
    cli -rpcwait -rpcwaittimeout=300 getblockchaininfo | grep -E '"chain"|"blocks"|"bestblockhash"'
    cli getconnectioncount | sed 's/^/ligações: /'
}

cmd_arrancar() {
    carregar
    local morada="" ignorar_hora=0
    while (( $# )); do
        case "$1" in
            --morada) morada=$2; shift 2 ;;
            --ignorar-hora) ignorar_hora=1; shift ;;   # só para o ensaio numa rede descartável
            *) falha "opção desconhecida: $1" ;;
        esac
    done
    [[ "${morada}" == pqc1p* ]] || falha "indica --morada pqc1p... (rede principal) de uma carteira fora das máquinas de teste."
    local agora; agora=$(date -u +%s)
    if (( agora < LAUNCH_EPOCH && ! ignorar_hora )); then
        falha "ainda não são 23:00 UTC de 27 nov. ($(date -u +%H:%M:%S) UTC agora). Ninguém mina antes."
    fi
    local v; v=$(versao_pqchd)
    [[ "${v}" =~ ^v[1-9] ]] || falha "o pqchd instalado é ${v:-desconhecido}: instala a 1.0.0 (binarios 1.0.0)."
    cli getblockchaininfo | grep -q '"chain": "main"' || falha "o nó da rede principal não responde (arrancar-no)."
    cli validateaddress "${morada}" | grep -q '"isvalid": true' || falha "morada inválida na rede principal: ${morada}"

    msg "1/2 Parar a mineração da testnet nesta máquina"
    local s parados=()
    for s in "${TESTNET_MINERS[@]}"; do
        if systemctl is-active --quiet "${s}" 2>/dev/null; then
            systemctl stop "${s}"; systemctl disable "${s}" >/dev/null 2>&1 || true
            parados+=("${s}"); echo "  ${s}: parado"
        fi
    done
    printf '%s\n' "${parados[@]}" > /var/lib/pqch-minerar-main.testnet-parados
    echo "  (o nó da testnet continua a correr, sem minerar; o Mac continua a minerar a testnet)"

    msg "2/2 Pool da rede principal (${THREADS} threads$( (( FAST )) && echo ', modo rápido'))"
    local extra=(); (( FAST )) && extra+=(--fast)
    "${POOL_REPO}/deploy/instalar_pool_testnet.sh" --rede main --user "${NODE_USER}" --datadir "${DATADIR}" \
        --morada "${morada}" --threads "${THREADS}" "${extra[@]}" --peer "${ANCORA_POOL}"
    echo
    echo "A minerar a rede principal. Partilhas e blocos: /var/lib/pqch-pool-main/pool.jsonl"
}

cmd_parar() {
    carregar
    msg "Parar o pool da rede principal"
    systemctl disable --now pqch-pool-main 2>/dev/null && echo "  pqch-pool-main: parado" || echo "  pqch-pool-main: não existia"
    if [[ -f /var/lib/pqch-minerar-main.testnet-parados ]]; then
        local s
        while read -r s; do
            [[ -n "${s}" ]] || continue
            systemctl enable --now "${s}" >/dev/null 2>&1 && echo "  ${s}: de volta à testnet"
        done < /var/lib/pqch-minerar-main.testnet-parados
        rm -f /var/lib/pqch-minerar-main.testnet-parados
    fi
    echo "O nó da rede principal continua ligado (não minera)."
}

cmd_estado() {
    msg "Estado"
    echo "pqchd instalado: $(versao_pqchd)"
    if [[ -f "${CFG}" ]]; then
        source "${CFG}"
        echo "Nó da rede principal: ${NODE_SVC} ($(systemctl is-enabled "${NODE_SVC}" 2>/dev/null || echo '?') / $(systemctl is-active "${NODE_SVC}" 2>/dev/null || true)), ${DATADIR}"
        echo "Pool: ${THREADS} threads$( (( FAST )) && echo ', modo rápido'); clone em ${POOL_REPO}"
        if systemctl is-active --quiet "${NODE_SVC}"; then
            cli getblockchaininfo 2>/dev/null | grep -E '"chain"|"blocks"' || true
        fi
    else
        echo "Ainda não preparado."
    fi
    local s
    for s in pqch-pool-main "${TESTNET_MINERS[@]}" pqchd-testnet p2pool xmrig; do
        systemctl list-unit-files "${s}.service" --no-legend 2>/dev/null | grep -q . \
            && echo "  ${s}: $(systemctl is-active "${s}" 2>/dev/null || true)"
    done
    echo "Portas à escuta 29332/29333 (a 29332 só em 127.0.0.1):"
    ss -ltnH 2>/dev/null | awk '{print $4}' | grep -E ':(29332|29333)$' || echo "  nenhuma"
    awk '/MemAvailable/ {printf "Memória disponível: %.1f GB\n", $2/1048576}' /proc/meminfo
}

case "${1:-}" in
    preparar) shift; cmd_preparar "$@" ;;
    binarios) shift; cmd_binarios "$@" ;;
    arrancar-no) cmd_arrancar_no ;;
    arrancar) shift; cmd_arrancar "$@" ;;
    parar) cmd_parar ;;
    estado) cmd_estado ;;
    *) sed -n '5,15p' "$0"; exit 1 ;;
esac
