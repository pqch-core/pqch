#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying file COPYING or
# https://opensource.org/license/mit/.
#
# Máquina de testes do PQCH: fuzzing contínuo + vigilância da testnet (README.md nesta pasta).
#
#   sudo contrib/pqch/servidor-testes/instalar.sh
#
# Pode correr-se outra vez sem estragar nada (reinstala os scripts e os serviços).
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Corre com sudo." >&2; exit 1; }
[[ "$(uname -m)" == x86_64 ]] || { echo "Só x86_64." >&2; exit 1; }
AQUI="$(cd "$(dirname "$0")" && pwd)"
VERSAO="${PQCH_VERSAO:-0.3.0}"
FPR="1CDFA6A84A35AA6D7B524D1CFB6E21DF0D38B5DA"   # chave de assinatura das versões (pqch-releases.asc)
REPO="https://github.com/pqch-core/pqch"
NPROC="$(nproc)"
QUOTA=$(( (NPROC > 1 ? NPROC - 1 : 1) * 100 ))

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
apt-get install -y -q --no-install-recommends \
    build-essential cmake pkgconf python3 libevent-dev libboost-dev ccache clang llvm git curl \
    ca-certificates gnupg tar unattended-upgrades bsdextrautils
CLANG_MAJOR="$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)"
apt-get install -y -q --no-install-recommends "libclang-rt-${CLANG_MAJOR}-dev"
dpkg-reconfigure -f noninteractive unattended-upgrades

echo "== 3/6 Utilizadores sem privilégios (pqch-fuzz, pqch-vigia)"
for u in pqch-fuzz pqch-vigia; do
    id "$u" >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin "$u"
done
install -d -o pqch-fuzz -g pqch-fuzz -m 0750 /var/lib/pqch-fuzz
install -d -o pqch-vigia -g pqch-vigia -m 0750 /var/lib/pqch-vigia /var/lib/pqch-vigia/no
FUZZ_HOME="$(getent passwd pqch-fuzz | cut -d: -f6)"
if [[ ! -d "$FUZZ_HOME/pqch/.git" ]]; then
    sudo -u pqch-fuzz -H git clone -q --filter=blob:none "$REPO.git" "$FUZZ_HOME/pqch"
fi

echo "== 4/6 PQCH ${VERSAO} (binários assinados) para o nó de vigilância"
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
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
CONF
chown pqch-vigia:pqch-vigia /var/lib/pqch-vigia/no/pqch.conf

echo "== 5/6 Scripts e serviços"
install -m 0755 "$AQUI/fuzz-ciclo.sh" /usr/local/lib/pqch-fuzz-ciclo
install -m 0755 "$AQUI/vigia.sh" /usr/local/lib/pqch-vigia
install -m 0755 "$AQUI/pqch-estado" /usr/local/bin/pqch-estado

cat > /etc/systemd/system/pqch-fuzz.service <<UNIT
[Unit]
Description=Fuzzing contínuo do PQCH (baixa prioridade)
After=network-online.target
Wants=network-online.target

[Service]
User=pqch-fuzz
WorkingDirectory=/var/lib/pqch-fuzz
Environment=CCACHE_DIR=${FUZZ_HOME}/.ccache
ExecStart=/usr/local/lib/pqch-fuzz-ciclo
Restart=always
RestartSec=60
Nice=19
CPUSchedulingPolicy=batch
IOSchedulingClass=idle
CPUQuota=${QUOTA}%
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

echo "== 6/6 Arrancar"
systemctl daemon-reload
systemctl enable --now pqch-vigia-no.service pqch-vigia.timer pqch-fuzz.service
systemctl --no-pager --lines=0 status pqch-vigia-no pqch-fuzz | grep -E "●|Active" || true
echo
echo "Pronto. Resumo a qualquer momento: sudo pqch-estado"
echo "A primeira compilação do fuzzing demora (ver: journalctl -u pqch-fuzz -f)."
