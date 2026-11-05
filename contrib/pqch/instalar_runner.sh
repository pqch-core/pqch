#!/usr/bin/env bash
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
#
# Instala o runner próprio do GitHub Actions para o repositório pqch-core/pqch (ver RUNNER.md).
#
#   sudo ./instalar_runner.sh <token-de-registo> [nome-do-runner]
#
# O token vem de GitHub > pqch-core/pqch > Settings > Actions > Runners > New self-hosted runner
# (linha "./config.sh --url ... --token XXXX"); vale uma hora.
#
# O que faz, por esta ordem:
#   1. instala os pacotes de compilação (uma vez; os jobs não usam sudo);
#   2. cria o utilizador "pqch-runner", sem sudo, dono de tudo o que o runner faz;
#   3. descarrega a versão mais recente do runner e confere o SHA256 publicado pelo GitHub;
#   4. regista o runner com as etiquetas self-hosted, linux, x64, pqch;
#   5. instala-o como serviço systemd, que arranca com a máquina.
# Pode correr outra vez: atualiza os pacotes e volta a registar (--replace) com o mesmo nome.

set -euo pipefail

REPO_URL="https://github.com/pqch-core/pqch"
RUNNER_USER="pqch-runner"
LABELS="pqch"

if [[ $EUID -ne 0 ]]; then
    echo "Corre com sudo: sudo $0 <token> [nome]" >&2
    exit 1
fi
TOKEN="${1:-}"
if [[ -z "$TOKEN" ]]; then
    read -rsp "Token de registo do runner: " TOKEN
    echo
fi
[[ -n "$TOKEN" ]] || { echo "Falta o token." >&2; exit 1; }
NAME="${2:-pqch-$(hostname -s)}"

case "$(uname -m)" in
    x86_64) ARCH=x64 ;;
    *) echo "Só x86_64 (o RandomX e o build do PQCH são testados nessa arquitetura)." >&2; exit 1 ;;
esac

echo "== 1/5 Pacotes"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
    build-essential cmake pkgconf python3 libevent-dev libboost-dev libsqlite3-dev ccache \
    clang llvm git curl ca-certificates jq tar  # llvm: llvm-symbolizer, for readable sanitizer reports
# Runtime dos sanitizers e do libFuzzer, na versão do clang instalado.
CLANG_MAJOR="$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)"
apt-get install -y -q --no-install-recommends "libclang-rt-${CLANG_MAJOR}-dev" || \
    echo "AVISO: libclang-rt-${CLANG_MAJOR}-dev não existe nesta distribuição; o job de fuzzing pode falhar."

echo "== 2/5 Utilizador ${RUNNER_USER}"
if ! id "$RUNNER_USER" >/dev/null 2>&1; then
    useradd --create-home --shell /bin/bash "$RUNNER_USER"
fi
HOME_DIR="$(getent passwd "$RUNNER_USER" | cut -d: -f6)"
RUNNER_DIR="${HOME_DIR}/actions-runner"
install -d -o "$RUNNER_USER" -g "$RUNNER_USER" "$RUNNER_DIR" "${HOME_DIR}/.ccache"

echo "== 3/5 Runner do GitHub Actions"
RELEASE_JSON="$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest)"
VERSION="$(jq -r .tag_name <<<"$RELEASE_JSON" | sed 's/^v//')"
TARBALL="actions-runner-linux-${ARCH}-${VERSION}.tar.gz"
# As notas de cada versão trazem o SHA256 de cada pacote entre marcadores <!-- BEGIN SHA linux-x64 -->.
EXPECTED_SHA="$(jq -r .body <<<"$RELEASE_JSON" | sed -n "s/.*<!-- BEGIN SHA linux-${ARCH} -->\([0-9a-f]\{64\}\)<!-- END SHA linux-${ARCH} -->.*/\1/p" | head -1)"
[[ ${#EXPECTED_SHA} -eq 64 ]] || { echo "Não encontrei o SHA256 publicado para ${TARBALL}; paro por segurança." >&2; exit 1; }
if [[ -x "${RUNNER_DIR}/svc.sh" ]]; then
    (cd "$RUNNER_DIR" && ./svc.sh stop >/dev/null 2>&1 || true)
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "${TMP}/${TARBALL}" "https://github.com/actions/runner/releases/download/v${VERSION}/${TARBALL}"
echo "${EXPECTED_SHA}  ${TMP}/${TARBALL}" | sha256sum -c -
tar -xzf "${TMP}/${TARBALL}" -C "$RUNNER_DIR"
chown -R "$RUNNER_USER:$RUNNER_USER" "$RUNNER_DIR"
"${RUNNER_DIR}/bin/installdependencies.sh" >/dev/null

echo "== 4/5 Registo (${NAME}, versão ${VERSION})"
sudo -u "$RUNNER_USER" -H bash -c "cd '$RUNNER_DIR' && ./config.sh --unattended --replace \
    --url '$REPO_URL' --token '$TOKEN' --name '$NAME' --labels '$LABELS' --work _work"

echo "== 5/5 Serviço"
cd "$RUNNER_DIR"
./svc.sh install "$RUNNER_USER"
./svc.sh start
./svc.sh status | head -5

cat <<EOF

Feito. Falta um passo no GitHub (uma vez):
  pqch-core/pqch > Settings > Secrets and variables > Actions > Variables > New repository variable
    Nome:  PQCH_RUNS_ON
    Valor: ["self-hosted","linux","x64","pqch"]
A partir daí os jobs do PQCH CI correm nesta máquina. Para voltar aos runners do GitHub, apaga a variável.
EOF
