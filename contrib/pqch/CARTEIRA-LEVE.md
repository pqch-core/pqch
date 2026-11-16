# Carteira leve PQCH (Android)

Carteira para telemóvel que **não guarda a blockchain**: guarda só as chaves (as 24 palavras) e
pede ao servidor do explorador as moedas e o histórico dos seus endereços. Envia e recebe; **não
mina** (a Google Play proíbe mineração no próprio telemóvel e um telemóvel mina muito pouco).

## Peças

| Peça | Onde | Estado |
|---|---|---|
| Núcleo (chaves, endereços, assinatura) | `src/pqchwallet/` → `libpqchwallet` | feito, testado contra o nó (`test/functional/wallet_pqch_lightwallet.py`) |
| Servidor de consulta | `pqch-electrs` (API Esplora: `/address/…/utxo`, `/address/…/txs`, `POST /tx`, taxas em `/v1/fees/recommended` ou `/fee-estimates`). Rede real: `https://explorer.pqch.org/api`. Testnet: `https://api-testnet.pqch.org/api` (só API, sem páginas, na VPS 72.61.20.80) | rede real: explorador; testnet: a montar (etapa 3) |
| App Android | `contrib/pqch/android/` (Kotlin, Jetpack Compose) | primeira versão de teste |
| Compilação do APK | `.github/workflows/android.yml` (runner de casa: JDK 17, Android SDK/NDK, Gradle) + `contrib/pqch/android/build-core.sh` | primeira versão |

## Porque o núcleo é o código do nó

As chaves P2MR são híbridas (Schnorr + ML-DSA-44, e SLH-DSA-128s na folha de recuperação) e
derivam de uma semente pós-quântica própria (`src/script/p2mr.h`), não de BIP32. Nenhuma carteira
existente faz isto. Reescrever a derivação e a assinatura noutra linguagem seria um segundo
código a auditar e a manter igual ao primeiro; por isso a app usa **o mesmo código C++ da carteira
do computador** (descritor `p2mr(pqprv(…)/360h/{coin}h/0h/{0h,1h}/*h)`, `SignTransaction`), compilado
para Android com o NDK, por trás de uma única função C com JSON (`pqchw_call`).

Consequências:
- As mesmas 24 palavras (e frase-passe opcional) dão **os mesmos endereços** no telemóvel e no
  PQCH Core: pode restaurar-se uma carteira num e noutro.
- O núcleo nunca fala com a rede; a app é que lê e envia pela API.

## Segurança (regras para a app)

- As palavras ficam cifradas no telemóvel com uma chave do Android Keystore, desbloqueada por PIN
  ou biometria; nunca saem do telemóvel, nunca vão para cópias de segurança na nuvem
  (`android:allowBackup="false"`), e o ecrã que as mostra bloqueia capturas (`FLAG_SECURE`).
- Ligação à API só por HTTPS, ao servidor do projeto (configurável para quem tiver o seu).
- Antes de assinar, a app mostra destino, valor e taxa; a taxa e o tamanho vêm do núcleo.
- Transações com RBF (a taxa pode ser aumentada depois).
- Privacidade: o servidor vê os endereços consultados (como em qualquer carteira leve). Quem
  quiser privacidade total usa o PQCH Core ou o seu próprio servidor.
- Antes do lançamento: revisão de segurança própria da app (está na lista da auditoria).

## Testes antes do lançamento

1. Núcleo: `wallet_pqch_lightwallet.py` (corre no CI).
2. APK de teste na **testnet pública**. O explorador `explorer.pqch.org` fica só para a rede real
   (decidido a 16 nov. 2026); a testnet tem uma API própria sem páginas, `api-testnet.pqch.org`, na
   VPS dos nós públicos. Os testes de saldo, envio e receção começam quando essa API estiver no ar.
   Até lá só se pode testar no telemóvel a parte que não usa a rede: criar e restaurar, PIN, receber
   (endereço e QR).
3. Restaurar no telemóvel uma carteira do PQCH Core e vice-versa; enviar e receber entre os dois.

## O que a app de teste faz (0.1)

- Criar carteira (24 palavras novas, confirmação de 3 palavras) ou restaurar (palavras + frase-passe opcional).
- PIN de 6 a 12 algarismos; a app bloqueia ao sair dela; 5 PIN errados → esperas crescentes.
- Saldo (disponível, pendente, minerado ainda imaturo), histórico, receber (endereço + QR, copiar),
  enviar (colar endereço, valor ou "tudo", taxa rápida/normal/económica, ecrã de confirmação).
- Definições: rede (testnet por omissão), servidor da API, mostrar as palavras (pede o PIN),
  remover a carteira do telemóvel.
- Enviar: ler o endereço com a câmara (**Scan QR code**; aceita também ligações `pqch:<endereço>?amount=…`).
  A câmara só é pedida da primeira vez que se usa o leitor.
- Ainda não: impressão digital, traduções, aumentar a taxa (RBF) a partir da app.

## Instalar o APK de teste

1. GitHub → Actions → **PQCH Android wallet** → a última execução verde → artefacto
   **pqch-wallet-apk** (ZIP com o APK e o `SHA256SUMS`).
2. Conferir o SHA256 do APK com o do ficheiro.
3. Passar o APK para o telemóvel (cabo USB) e abri-lo; o Android pede para autorizar
   "instalar apps desta origem" para o gestor de ficheiros (só para este teste).
4. Usar só na testnet e sem valor real.

## Assinar uma versão (APK de release)

O APK de teste é assinado com a chave de debug do runner. As versões publicadas são assinadas com a
**chave de release da app**, que fica só nas pens USB (como a chave GPG das releases) e nunca passa
pelo runner nem pelo GitHub.

Porque importa: o Android só instala uma atualização se vier assinada com a mesma chave da versão
instalada.
- Quem **perder** a chave nunca mais consegue atualizar a app nos telemóveis: teria de publicar
  outra app e pedir a toda a gente para mudar.
- Quem a **roubar** consegue publicar uma "atualização" falsa, que o Android aceita.

Por isso: duas pens (`D:` e `E:`), a palavra-passe só no Bitwarden, nunca no repositório, no runner
nem na conversa.

### Uma vez: ferramentas no PC das releases (Windows)

1. **Java 17:** instalador MSI do Temurin 17 (adoptium.net). Traz o `keytool`.
2. **apksigner:** "Command line tools only" da página oficial do Android Studio
   (developer.android.com/studio). Conferir o SHA-256 do ZIP com o da página. Extrair para
   `C:\Android\cmdline-tools\latest\`, e depois:
   ```
   C:\Android\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=C:\Android "build-tools;35.0.0"
   ```
   O `apksigner.bat` fica em `C:\Android\build-tools\35.0.0\`.

### Uma vez: criar a chave (com a pen D: ligada)

```
mkdir D:\android-pqch
keytool -genkeypair -v -storetype PKCS12 -keystore D:\android-pqch\pqch-wallet-release.jks -alias pqch-wallet -keyalg RSA -keysize 4096 -validity 36500 -dname "CN=PQCH Releases, O=PQCH"
```
- Palavra-passe nova e forte, guardada no Bitwarden (a mesma serve para o ficheiro e para a chave).
- Validade de 100 anos: o Android não deixa trocar a chave de uma app já instalada.
- Copiar `D:\android-pqch\` para `E:\android-pqch\` e confirmar que as duas cópias abrem:
  `keytool -list -v -keystore E:\android-pqch\pqch-wallet-release.jks -alias pqch-wallet`.
- A impressão digital **SHA-256 do certificado** que esse comando mostra publica-se nas notas de
  versão e na página da testnet: é o que permite a cada pessoa confirmar que o APK é do projeto.

**Chave criada a 16 nov. 2026; ensaio de assinatura completo no mesmo dia (APK 0.3.0 do run #4)** (RSA 4096, SHA384withRSA, alias `pqch-wallet`, nas pens D: e E:).
Impressão digital SHA-256 do certificado, a que todo o APK de release tem de mostrar:

```
39:86:31:73:B5:7A:8D:95:92:E4:FF:5C:84:D8:6D:87:27:82:BC:51:8A:7C:90:DE:F9:E2:F0:E7:3B:F3:D2:ED
```

### Em cada versão

1. GitHub → Actions → **PQCH Android wallet** → *Run workflow*, com **release** marcado e a versão
   (por exemplo `0.3.0`). O artefacto traz `pqch-wallet-0.3.0-unsigned.apk` e o `SHA256SUMS`.
2. No PC, conferir o SHA-256 do APK com o `SHA256SUMS` do artefacto.
3. Assinar (pen D: ligada). A palavra-passe entra pelo `Read-Host`, que aceita Ctrl+V (colar na
   pergunta do próprio `apksigner` falhou no ensaio de 16 nov. 2026), e é apagada no fim:
   ```
   $s = Read-Host "Palavra-passe da chave" -AsSecureString
   $env:PQCH_KS_PASS = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
   C:\Android\build-tools\35.0.0\apksigner.bat sign --ks D:\android-pqch\pqch-wallet-release.jks --ks-key-alias pqch-wallet --ks-pass env:PQCH_KS_PASS --out pqch-wallet-0.3.0.apk pqch-wallet-0.3.0-unsigned.apk
   Remove-Item Env:PQCH_KS_PASS; Remove-Variable s
   C:\Android\build-tools\35.0.0\apksigner.bat verify --verbose --print-certs pqch-wallet-0.3.0.apk
   ```
   O `verify` tem de dizer `v2 … true` e `v3 … true` (o v1 fica `false`: não é preciso a partir do
   Android 8, o mínimo da app) e `Signer #1 certificate SHA-256 digest:
   39863173b57a8d9592e4ff5c84d86d872782bc518a7c90def9e2f0e73bf3d2ed`. Fechar a janela do PowerShell.
4. Juntar o APK assinado à release do PQCH Core **antes** de assinar o `SHA256SUMS` com a chave GPG
   (`RELEASE.md`): acrescentar a linha `Get-FileHash` do APK ao `SHA256SUMS` e só depois assinar.
5. Tirar a pen.

Quem tem instalado um APK de teste (chave de debug) tem de o **desinstalar** antes de instalar o
primeiro APK de release: as chaves são diferentes. Os dados da carteira de teste perdem-se (as
palavras de recuperação continuam a restaurá-la).
