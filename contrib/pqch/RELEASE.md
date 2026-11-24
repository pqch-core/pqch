# Versões (instaladores) do PQCH Core

O workflow **PQCH Release** (`.github/workflows/release.yml`) compila o PQCH Core (aplicação
gráfica e nó) no runner do projeto:

| Ficheiro | Para quê |
|---|---|
| `pqch-<versão>-win64-setup.exe` | instalador Windows (NSIS) |
| `pqch-<versão>-win64.zip` | os mesmos programas, sem instalar |
| `pqch-<versão>-x86_64-linux-gnu.tar.gz` | Linux x86_64 (Qt incluído, corre na maioria das distribuições) |
| `SHA256SUMS` | o SHA256 de cada ficheiro |
| `SHA256SUMS.asc` | a assinatura do `SHA256SUMS`, feita **no teu computador** (passo 3) |

macOS fica para depois do lançamento.

**Porque é que o runner não assina:** a máquina de CI executa código de cada push. Se a chave de
assinatura estivesse lá, quem comprometesse a máquina podia assinar instaladores falsos. A chave
fica só no teu computador (e numa cópia offline).

## 1. Uma vez: preparar o runner

Na máquina do runner (os jobs não usam sudo, por isso os pacotes instalam-se à mão):

```bash
sudo apt-get install -y --no-install-recommends bison byacc gperf xz-utils patch \
    g++-mingw-w64-x86-64-posix nsis zip unzip
```

(`contrib/pqch/instalar_runner.sh` já os instala em runners novos.)

Primeiro ensaio: **Actions › PQCH Release › Run workflow** em `main`. Gera uma versão
`dev-<commit>` só como artefacto. A primeira vez compila o Qt e demora 1 a 2 horas por plataforma;
as seguintes reutilizam a cache em `/home/pqch-runner/pqch-release-cache` (cerca de 10 GB).

## 2. Uma vez: a chave de assinatura

No teu computador, não no runner nem nos servidores.
- **Linux:** `gnupg`.
- **Windows:** [Gpg4win](https://gpg4win.org); usa a "Linha de comandos" do Kleopatra ou o PowerShell.

```bash
gpg --quick-generate-key "PQCH Releases <releases@pqch.org>" ed25519 sign 2y
gpg --fingerprint releases@pqch.org          # anota a impressão digital (40 caracteres)
gpg --armor --export releases@pqch.org > pqch-releases.asc
gpg --armor --export-secret-keys releases@pqch.org > CHAVE-PRIVADA-pqch-releases.asc
```

- `pqch-releases.asc` (pública) vai para o site, com a impressão digital escrita na página de downloads, e
  para `contrib/pqch/pqch-releases.asc` neste repositório: o workflow junta-a a cada Release.
- Chave atual (8 nov. 2026): impressão digital `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`, válida até 7 nov. 2028.
- O texto das Releases sai de `contrib/pqch/release-notes.md`; antes de publicar, preenche a lista "Changes".
- `CHAVE-PRIVADA-…` vai para uma pen USB guardada offline, e apaga-se do disco. Sem ela não há
  assinaturas novas; se alguém a obtiver, pode assinar em nome do projeto.
- O GnuPG cria também um certificado de revogação (pasta `openpgp-revocs.d`). Guarda-o com a cópia offline.

## 3. Cada versão

1. Acerta a versão em `CMakeLists.txt` (`CLIENT_VERSION_MAJOR/MINOR/BUILD`) e faz commit em `main`.
2. Cria e envia a tag (tem de coincidir com a versão; o workflow recusa se não coincidir):
   ```bash
   git tag v1.0.0 && git push origin v1.0.0
   ```
   Sem acesso ao git pela linha de comandos: no GitHub, **Actions → PQCH Release → Run workflow**,
   ramo `main`, com a caixa **release** marcada. O rascunho fica com a tag `v<versão>` apontada a esse
   commit; o GitHub só cria a tag quando o rascunho for publicado.
3. Quando o workflow acabar, há um **rascunho** em **Releases** com os ficheiros e o `SHA256SUMS`.
   Descarrega tudo para uma pasta e confere:
   ```bash
   sha256sum -c SHA256SUMS          # todos "OK"
   ```
4. Assina:
   ```bash
   gpg --armor --detach-sign --local-user releases@pqch.org SHA256SUMS     # cria SHA256SUMS.asc
   gpg --verify SHA256SUMS.asc SHA256SUMS                                   # "Good signature"
   ```
5. Junta o `SHA256SUMS.asc` ao rascunho e copia os ficheiros para a página de downloads do site.
   Publica o rascunho só se o repositório já for público.

## Como os utilizadores verificam

```bash
gpg --import pqch-releases.asc            # uma vez; confirma a impressão digital no site
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum --ignore-missing -c SHA256SUMS
```

No Windows, sem GnuPG: `certutil -hashfile pqch-1.0.0-win64-setup.exe SHA256` e comparar com a
linha do `SHA256SUMS`.

**Aviso do Windows (SmartScreen e Controlo Inteligente de Aplicações):** o instalador ainda não tem
certificado de assinatura de código, por isso o Windows mostra "Editor desconhecido". No Windows 11 com o
Controlo Inteligente de Aplicações ligado, o `pqch-qt.exe` é bloqueado e não há exceção por aplicação
(visto no teste de 24 nov. 2026). A garantia é a assinatura do `SHA256SUMS`. A página de downloads
(`dl_win_note`) e as notas da versão dizem-no. Plano: certificado de assinatura de código em nome
individual e uma versão assinada com o mesmo código.
