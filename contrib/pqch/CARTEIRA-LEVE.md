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
- Ainda não: ler QR com a câmara, impressão digital, traduções, aumentar a taxa (RBF) a partir da app.

## Instalar o APK de teste

1. GitHub → Actions → **PQCH Android wallet** → a última execução verde → artefacto
   **pqch-wallet-apk** (ZIP com o APK e o `SHA256SUMS`).
2. Conferir o SHA256 do APK com o do ficheiro.
3. Passar o APK para o telemóvel (cabo USB) e abri-lo; o Android pede para autorizar
   "instalar apps desta origem" para o gestor de ficheiros (só para este teste).
4. Usar só na testnet e sem valor real.
