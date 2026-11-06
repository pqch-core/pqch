# PQCH — Identidade e testnet

## Nomes

| Programa | Bitcoin Core | PQCH |
|---|---|---|
| Nó | `bitcoind` | `pqchd` |
| Linha de comandos | `bitcoin-cli` | `pqch-cli` |
| Ferramenta de carteira | `bitcoin-wallet` | `pqch-wallet` |
| Lançador | `bitcoin` | `pqch` |
| Testes unitários | `test_bitcoin` | `test_pqch` |
| Outros | `bitcoin-tx`, `bitcoin-util`, `bitcoin-qt`, `bench_bitcoin` | `pqch-tx`, `pqch-util`, `pqch-qt`, `bench_pqch` |

Os alvos do CMake mantêm os nomes do Bitcoin Core (por exemplo `cmake --build build --target
test_bitcoin`); só os ficheiros gerados mudam de nome. Assim os ficheiros de compilação herdados e as
futuras fusões com o Bitcoin Core ficam simples.

O agente de utilizador na rede é `/PQCH:<versão>/` (Bitcoin Core: `/Satoshi:<versão>/`). A versão
ainda é a do Bitcoin Core em que o PQCH se baseia (32.1.0); a numeração própria fica para o lançamento.

## Pasta de dados e configuração

| Sistema | Pasta |
|---|---|
| Linux | `~/.pqch` |
| macOS | `~/Library/Application Support/PQCH` |
| Windows | `%LOCALAPPDATA%\PQCH` |

- Ficheiro de configuração: `pqch.conf`. Ficheiro PID: `pqchd.pid`.
- Subpastas: `testnet/` e `regtest/`. A rede principal usa a própria pasta.
- O PQCH nunca usa a pasta do Bitcoin Core (`~/.bitcoin`), onde pode haver carteiras de Bitcoin. Se
  alguém apontar `-datadir` para uma pasta de outra rede, o nó para ao arrancar com "Incorrect or no
  genesis block found", antes de abrir as carteiras.

## Redes

| Rede | Opção | P2P | RPC | Endereços | Bytes mágicos |
|---|---|---|---|---|---|
| Principal | (nenhuma) | 29333 | 29332 | `pqc1…` | `a7 ca dc bf` |
| Testnet | `-testnet` | 39333 | 39332 | `pqct1…` | `e8 f4 ed 9b` |
| Regtest | `-regtest` | 49444 | 49443 | `pqcrt1…` | `a2 f4 92 d4` |

A testnet4 e a signet do Bitcoin não existem no PQCH: `-testnet4`, `-signet` ou `-chain=signet`
dão erro. O código delas fica só para os testes unitários herdados.

### Portas: conferência

Portas por omissão de redes conhecidas (P2P / RPC), para não colidir:

| Rede | Principal | Testnet / stagenet |
|---|---|---|
| Bitcoin | 8333 / 8332 | 18333, 48333, 38333 / 18332, 48332, 38332 |
| Monero | 18080 / 18081 | 28080 / 28081, 38080 / 38081 |
| P2Pool (Monero) | 37889 / 37888 (mini) | — |
| Litecoin | 9333 / 9332 | 19335 / 19332 |
| Dogecoin | 22556 / 22555 | 44556 / 44555 |
| Zcash | 8233 / 8232 | 18233 / 18232 |
| Dash | 9999 / 9998 | 19999 / 19998 |
| Tari | 18189, 18141–18143 | — |
| Ethereum (execução) | 30303 | — |

As portas do PQCH (29332–29333, 39332–39333, 49443–49444) não coincidem com nenhuma destas. A lista
foi feita com o que se sabe das redes; antes do lançamento confere-se outra vez com o registo da IANA e
com as redes que entretanto surgirem.

## Testnet

Mesmas regras de consenso da rede principal (RandomX, ASERT, P2MR, AuxPoW, reorganização máxima de
60 blocos). Diferenças:

- Moedas sem valor.
- Maturação das recompensas: 100 blocos (rede principal: 720), para os testadores poderem gastar no
  próprio dia.
- Bloco génese próprio, com uma manchete do The Times do dia em que a testnet foi criada.
- Pode ser recomeçada com um génese novo, se for preciso.

### Como as pessoas encontram a testnet

Um nó novo procura outros nós por esta ordem:

1. **`-addnode` / `-connect`:** endereços indicados à mão. É assim no início.
2. **Sementes de DNS:** nomes (por exemplo `seed.testnet.<domínio>`) que devolvem endereços de nós
   ativos. Exigem um domínio e um servidor que vá vendo que nós estão de pé.
3. **Sementes fixas:** lista de endereços escrita no código, usada quando o DNS falha.
4. **Troca de endereços:** depois de ligado a um nó, o nó novo recebe endereços de outros e passa a
   encontrá-los sozinho.

Plano:

1. **Primeiro nó público:** um servidor com IP fixo e a porta 39333 aberta (só esta; a RPC nunca).
   Fica no código como semente fixa e é publicado na documentação.
2. **Instruções para testadores:** `pqchd -testnet -addnode=<ip>:39333`, ou só `pqchd -testnet`
   quando houver sementes.
3. **Mais nós:** dois ou três em sítios diferentes, para a rede não depender de uma máquina.
4. **Semente de DNS:** quando houver um domínio para o projeto.
5. **Torneira (faucet) e explorador de blocos:** uma página que dá moedas de teste e outra que
   mostra blocos e transações.
