# Carteira leve PQCH (Android)

Carteira para telemóvel que **não guarda a blockchain**: guarda só as chaves (as 24 palavras) e
pede ao servidor do explorador as moedas e o histórico dos seus endereços. Envia e recebe; **não
mina** (a Google Play proíbe mineração no próprio telemóvel e um telemóvel mina muito pouco).

## Peças

| Peça | Onde | Estado |
|---|---|---|
| Núcleo (chaves, endereços, assinatura) | `src/pqchwallet/` → `libpqchwallet` | feito, testado contra o nó (`test/functional/pqch_lightwallet.py`) |
| Servidor de consulta | `pqch-electrs` + API do explorador (`/api/address/…/utxo`, `/api/address/…/txs`, `POST /api/tx`, `/api/v1/fees/recommended`) | já existe (explorador) |
| App Android | `contrib/pqch/android/` (Kotlin) | a fazer |
| Compilação do APK | workflow no runner de casa (JDK + Android SDK/NDK) | a fazer |

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

1. Núcleo: `pqch_lightwallet.py` (corre no CI).
2. APK de teste na testnet fechada, instalado à mão nos telemóveis da equipa (com o WireGuard
   para chegar ao explorador da testnet).
3. Restaurar no telemóvel uma carteira do PQCH Core e vice-versa; enviar e receber entre os dois.
