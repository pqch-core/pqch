#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Máquinas de testes do PQCH (README.md nesta pasta). Duas funções, uma por máquina:
#
#   sudo contrib/pqch/servidor-testes/instalar.sh vigiar    # fuzzing contínuo + vigilância da testnet
#   sudo contrib/pqch/servidor-testes/instalar.sh minerar   # minerar a testnet
#
# Pode correr-se outra vez sem estragar nada (reinstala os scripts e os serviços).
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Corre com sudo." >&2; exit 1; }
[[ "$(uname -m)" == x86_64 ]] || { echo "Só x86_64." >&2; exit 1; }
AQUI="$(cd "$(dirname "$0")" && pwd)"
FUNCAO="${1:-}"
[[ "$FUNCAO" == vigiar || "$FUNCAO" == minerar ]] || { echo "uso: sudo $0 vigiar|minerar" >&2; exit 1; }
KALI=0; grep -q '^ID=kali' /etc/os-release && KALI=1
RAM_GB=$(( $(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024 / 1024 ))
VERSAO="${PQCH_VERSAO:-0.3.0}"
FPR="1CDFA6A84A35AA6D7B524D1CFB6E21DF0D38B5DA"   # chave de assinatura das versões (pqch-releases.asc)
REPO="https://github.com/pqch-core/pqch"
NPROC="$(nproc)"

echo "== 1/6 Limpar o runner antigo do GitHub Actions"
if systemctl list-unit-files 'actions.runner.*' --no-legend 2>/dev/null | grep -q .; then
    echo "AVISO: ainda há um serviço actions.runner instalado; retira-o primeiro (RUNNER.md)." >&2; exit 1
fi
if id pqch-runner >/dev/null 2>&1; then
    userdel -r pqch-runner 2>/dev/null || userdel pqch-runner
    rm -rf /home/pqch-runner
    echo "utilizador pqch-runner e a sua pasta apagados"
else
    echo "já não existe"
fi

echo "== 2/6 Pacotes e atualizações automáticas de segurança"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends curl ca-certificates gnupg tar python3 bsdextrautils
if [[ "$FUNCAO" == vigiar ]]; then
    apt-get install -y -q --no-install-recommends \
        build-essential cmake pkgconf libevent-dev libboost-dev ccache clang llvm git
    CLANG_MAJOR="$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)"
    apt-get install -y -q --no-install-recommends "libclang-rt-${CLANG_MAJOR}-dev"
fi
if [[ $KALI == 1 ]]; then
    echo "Kali (rolling): sem atualizações automáticas; atualiza à mão com apt."
else
    apt-get install -y -q --no-install-recommends unattended-upgrades
    dpkg-reconfigure -f noninteractive unattended-upgrades
fi

echo "== 3/6 Utilizadores sem privilégios"
if [[ "$FUNCAO" == vigiar ]]; then
    for u in pqch-fuzz pqch-vigia; do
        id "$u" >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin "$u"
    done
    install -d -o pqch-fuzz -g pqch-fuzz -m 0750 /var/lib/pqch-fuzz
    install -d -o pqch-vigia -g pqch-vigia -m 0750 /var/lib/pqch-vigia /var/lib/pqch-vigia/no
    FUZZ_HOME="$(getent passwd pqch-fuzz | cut -d: -f6)"
    if [[ ! -d "$FUZZ_HOME/pqch/.git" ]]; then
        sudo -u pqch-fuzz -H git clone -q --filter=blob:none "$REPO.git" "$FUZZ_HOME/pqch"
    fi
else
    id pqch-minerador >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin pqch-minerador
    install -d -o pqch-minerador -g pqch-minerador -m 0750 /var/lib/pqch-minerador
fi

echo "== 4/6 PQCH ${VERSAO} (binários assinados)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
TAR="pqch-${VERSAO}-x86_64-linux-gnu.tar.gz"
for f in "$TAR" SHA256SUMS SHA256SUMS.asc pqch-releases.asc; do
    curl -fsSL -o "$TMP/$f" "$REPO/releases/download/v${VERSAO}/$f"
done
export GNUPGHOME="$TMP/gpg"; install -d -m 700 "$GNUPGHOME"
gpg -q --import "$TMP/pqch-releases.asc"
gpg --status-fd 1 --verify "$TMP/SHA256SUMS.asc" "$TMP/SHA256SUMS" 2>/dev/null | grep -q "VALIDSIG $FPR" \
    || { echo "ERRO: a assinatura do SHA256SUMS não é da chave $FPR" >&2; exit 1; }
( cd "$TMP" && grep " ${TAR}\$" SHA256SUMS | sha256sum -c - ) || { echo "ERRO: o ficheiro não confere" >&2; exit 1; }
tar -xzf "$TMP/$TAR" -C "$TMP"
install -m 0755 "$TMP/pqch-${VERSAO}/bin/pqchd" "$TMP/pqch-${VERSAO}/bin/pqch-cli" /usr/local/bin/
unset GNUPGHOME

if [[ "$FUNCAO" == vigiar ]]; then
cat > /var/lib/pqch-vigia/no/pqch.conf <<CONF
# Nó de vigilância: sem carteira, sem portas abertas; entra pelas sementes públicas.
testnet=1
server=1
[test]
listen=0
disablewallet=1
dbcache=100
maxmempool=50
maxconnections=12
rpcport=39342
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
CONF
chown pqch-vigia:pqch-vigia /var/lib/pqch-vigia/no/pqch.conf
else
THREADS="${PQCH_THREADS:-$(( $(nproc) / 2 > 0 ? $(nproc) / 2 : 1 ))}"
cat > /var/lib/pqch-minerador/pqch.conf <<CONF
# Minerador da testnet: sem portas abertas; entra pelas sementes públicas. Só moedas de teste.
testnet=1
server=1
[test]
listen=0
dbcache=300
maxconnections=16
randomxfastmining=1
randomxminingthreads=${THREADS}
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
CONF
chown pqch-minerador:pqch-minerador /var/lib/pqch-minerador/pqch.conf
echo "minerador: ${THREADS} threads (uma por núcleo físico; o RandomX quer ~2 MB de cache L3 por thread)"
fi

echo "== 5/6 Scripts e serviços"
install -m 0755 "$AQUI/pqch-estado" /usr/local/bin/pqch-estado
if [[ "$FUNCAO" == vigiar ]]; then
# Pouca memória (< 12 GB): 2 processos de fuzzing de 2 GB; senão todos os núcleos menos um.
if (( RAM_GB < 12 )); then FUZZ_JOBS=2; FUZZ_RSS=2000; else FUZZ_JOBS=$(( NPROC > 1 ? NPROC - 1 : 1 )); FUZZ_RSS=3000; fi
(( FUZZ_JOBS <= NPROC )) || FUZZ_JOBS=$NPROC
install -m 0755 "$AQUI/fuzz-ciclo.sh" /usr/local/lib/pqch-fuzz-ciclo
install -m 0755 "$AQUI/vigia.sh" /usr/local/lib/pqch-vigia

cat > /etc/systemd/system/pqch-fuzz.service <<UNIT
[Unit]
Description=Fuzzing contínuo do PQCH (baixa prioridade)
After=network-online.target
Wants=network-online.target

[Service]
User=pqch-fuzz
WorkingDirectory=/var/lib/pqch-fuzz
Environment=CCACHE_DIR=${FUZZ_HOME}/.ccache
Environment=PQCH_FUZZ_JOBS=${FUZZ_JOBS} PQCH_FUZZ_RSS=${FUZZ_RSS}
ExecStart=/usr/local/lib/pqch-fuzz-ciclo
Restart=always
RestartSec=60
Nice=19
CPUSchedulingPolicy=batch
IOSchedulingClass=idle
CPUQuota=$(( FUZZ_JOBS * 100 ))%
MemoryMax=70%
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/pqch-fuzz ${FUZZ_HOME}

[Install]
WantedBy=multi-user.target
UNIT

cat > /etc/systemd/system/pqch-vigia-no.service <<UNIT
[Unit]
Description=Nó PQCH testnet de vigilância (sem carteira, sem portas abertas)
After=network-online.target
Wants=network-online.target

[Service]
User=pqch-vigia
ExecStart=/usr/local/bin/pqchd -conf=/var/lib/pqch-vigia/no/pqch.conf -datadir=/var/lib/pqch-vigia/no
Restart=on-failure
Nice=5
MemoryMax=2G
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/pqch-vigia

[Install]
WantedBy=multi-user.target
UNIT

cat > /etc/systemd/system/pqch-vigia.service <<UNIT
[Unit]
Description=Vigilância da testnet PQCH (uma verificação)
After=pqch-vigia-no.service

[Service]
Type=oneshot
User=pqch-vigia
ExecStart=/usr/local/lib/pqch-vigia
UNIT

cat > /etc/systemd/system/pqch-vigia.timer <<UNIT
[Unit]
Description=Vigilância da testnet PQCH, de hora a hora

[Timer]
OnBootSec=10min
OnUnitActiveSec=1h
Persistent=true

[Install]
WantedBy=timers.target
UNIT

else
install -m 0755 "$AQUI/minerar.sh" /usr/local/lib/pqch-minerar
cat > /etc/systemd/system/pqchd-testnet.service <<UNIT
[Unit]
Description=Nó PQCH testnet do minerador (sem portas abertas)
After=network-online.target
Wants=network-online.target pqch-minerar-testnet.service

[Service]
User=pqch-minerador
ExecStart=/usr/local/bin/pqchd -conf=/var/lib/pqch-minerador/pqch.conf -datadir=/var/lib/pqch-minerador
Restart=on-failure
TimeoutStopSec=300
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/pqch-minerador

[Install]
WantedBy=multi-user.target
UNIT
cat > /etc/systemd/system/pqch-minerar-testnet.service <<UNIT
[Unit]
Description=Minerador da testnet PQCH (generatetoaddress, só moedas de teste)
After=pqchd-testnet.service
PartOf=pqchd-testnet.service

[Service]
User=pqch-minerador
ExecStart=/usr/local/lib/pqch-minerar
Restart=always
RestartSec=30
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/pqch-minerador

[Install]
WantedBy=pqchd-testnet.service
UNIT
# Desempenho do RandomX: páginas grandes transparentes e o processador sempre na frequência máxima.
cat > /etc/systemd/system/pqch-desempenho.service <<UNIT
[Unit]
Description=PQCH: modo de mineração (performance, THP)
Before=pqchd-testnet.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'echo always > /sys/kernel/mm/transparent_hugepage/enabled; for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do [ -w "\$\$g" ] && echo performance > "\$\$g"; done; true'

[Install]
WantedBy=multi-user.target
UNIT
fi

echo "== 6/6 Arrancar"
systemctl daemon-reload
if [[ "$FUNCAO" == vigiar ]]; then
    systemctl enable --now pqch-vigia-no.service pqch-vigia.timer pqch-fuzz.service
    echo "Fuzzing: ${FUZZ_JOBS} processos de ${FUZZ_RSS} MB. A primeira compilação demora (journalctl -u pqch-fuzz -f)."
else
    systemctl enable --now pqch-desempenho.service pqchd-testnet.service pqch-minerar-testnet.service
    echo "O nó sincroniza primeiro; depois o minerador arranca sozinho (journalctl -u pqch-minerar-testnet -f)."
fi
echo
echo "Pronto. Resumo a qualquer momento: sudo /usr/local/bin/pqch-estado"
