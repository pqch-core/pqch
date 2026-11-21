# Pool descentralizado e aplicação de mineração (etapa 5)

Decidido a 18 nov. 2026: o pool descentralizado prometido no white paper (secção 2.1, crítica 4, e
secção 8) é um **fork do P2Pool** (SChernykh, GPL-3) com a PQCH como cadeia principal, e a
mineração faz-se numa **aplicação própria de um clique** para Windows e Linux, ao estilo do Tari
Universe. O merged mining com o Monero continua a fazer-se com o P2Pool oficial (`FASE5.md`): o pool
da PQCH é para minerar PQCH **diretamente**, sem operador.

## 1. O P2Pool e o que muda

Estudado na versão `0fbec27` (24 out. 2026), ~32.000 linhas de C++. O P2Pool tem duas camadas:

**A cadeia de partilhas** (sidechain), que é o que o torna descentralizado. Os mineiros do pool
mantêm entre si uma cadeia própria de "partilhas" (shares) muito mais fácil do que a cadeia
principal, uma a cada 10 segundos. Cada partilha diz quem a minerou. Quando uma partilha também
cumpre a dificuldade da cadeia principal, é um bloco verdadeiro, e a coinbase desse bloco paga a
todos os mineiros das últimas partilhas (PPLNS: os últimos N), na proporção do trabalho. Não há
operador nem conta: o pagamento está no próprio bloco.

**A ligação ao Monero**: o modelo de bloco, a coinbase, as moradas e a prova de trabalho.

| Parte do P2Pool | Ficheiros | O que fazer na PQCH |
| --- | --- | --- |
| Cadeia de partilhas, PPLNS, dificuldade das partilhas, tios (uncles) | `side_chain.cpp` (2.800 linhas) | **Fica**, com os parâmetros da PQCH (abaixo) |
| Rede entre nós do pool (gossip, sincronização) | `p2p_server.cpp`, `tcp_server.cpp`, `tls.cpp` | **Fica**; muda o formato das partilhas e o identificador da rede |
| Servidor stratum para os mineiros | `stratum_server.cpp` | **Fica**, com o blob de 80 bytes da PQCH |
| Modelo de bloco | `block_template.cpp` (1.600) | **Reescrever** a partir do `getblocktemplate` do `pqchd` |
| Formato do bloco e da partilha | `pool_block.cpp`, `pool_block_parser.inl` | **Reescrever**: cabeçalho de 80 bytes, transações à maneira do Bitcoin |
| Ligação ao nó (RPC, ZMQ) | `p2pool.cpp`, `zmq_reader.cpp`, `json_rpc_request.cpp` | **Adaptar**: `getblocktemplate`, `submitblock`, `-zmqpubhashblock` do `pqchd` (com cookie de autenticação) |
| Moradas e pagamentos com chaves de uso único | `wallet.cpp`, `crypto.cpp` | **Substituir**: na PQCH o mineiro é identificado pela morada P2MR e cada pagamento é uma saída P2MR normal, sem chaves de uso único (muito mais simples de verificar) |
| Prova de trabalho | `pow_hash.cpp` | **Adaptar**: RandomX v2 sobre o cabeçalho de 80 bytes, com a chave do `randomx_seed_hash` do `getblocktemplate` (calendário PQCH: 4.096 blocos, atraso 64) |
| Merged mining (Tari e outros) | `merge_mining_client*.cpp` | **Retirar** na primeira versão |

### Coinbase de um bloco do pool

- Uma saída P2MR para cada mineiro da janela PPLNS, ordenadas de forma determinística.
- O compromisso SegWit (obrigatório: SegWit está ativo desde o génesis).
- O identificador da partilha (o hash da sidechain) e o extranonce no `scriptSig` da coinbase. No
  Monero ficam no `tx_extra`.
- Cada saída P2MR ocupa 43 bytes. Com um teto de **500 mineiros por bloco** a coinbase fica em ~22 kB,
  cerca de 4% do peso máximo de um bloco (2.000.000). Os mineiros com trabalho abaixo do teto
  acumulam para o bloco seguinte (como o P2Pool faz com saídas pequenas).
- As recompensas só se gastam ao fim de 720 blocos (12 horas), como qualquer coinbase da PQCH.

### Parâmetros da cadeia de partilhas (a calibrar na testnet)

| Parâmetro | P2Pool mini (Monero) | Proposta PQCH | Porquê |
| --- | --- | --- | --- |
| Tempo por partilha | 10 s | 10 s | 6 partilhas por bloco de 1 minuto |
| Janela PPLNS | 2.160 partilhas (6 h) | 2.160 partilhas (6 h) | Igual; ~360 blocos PQCH |
| Dificuldade mínima | 100.000 | a medir | Depende do hashrate dos computadores comuns |
| Tios (uncles) | até 3 de profundidade | igual | Partilhas atrasadas continuam a contar |

### O programa que mina

O `xmrig` não serve tal como está: espera um blob do Monero com o nonce no byte 39, e a PQCH tem o
nonce no byte 76 do cabeçalho de 80 bytes. Duas saídas:

1. **Minerador próprio** (recomendado): um programa pequeno, licença MIT, feito a partir do minerador
   RandomX que já existe no PQCH Core (`generatetoaddress`, modo rápido, várias threads), a falar
   stratum com o pool local. Fica com o mesmo código já revisto e testado.
2. Um *patch* ao `xmrig` com um algoritmo "rx/pqch". Mais rápido nalgumas máquinas, mas é código de
   terceiros a manter. Fica para depois, se os mineiros o pedirem.

## 2. A aplicação de um clique

Para quem não sabe o que é um nó. Instala, escolhe ou cria a carteira, carrega em **Minerar**.

Por baixo corre, sem o utilizador ver:

1. `pqchd` (o nó completo, com pruning se o disco for pequeno);
2. `pqch-pool` (o fork do P2Pool), ligado ao `pqchd` e a outros nós do pool;
3. o minerador, ligado ao `pqch-pool` local.

O que o utilizador vê:

- **Carteira:** criar uma nova (24 palavras, com a folha de recuperação) ou colar uma morada que já
  tem. A aplicação só precisa da morada para receber.
- **Minerar / Parar**, e quanto do processador usar (baixo, médio, máximo), com o modo rápido do
  RandomX só se houver memória.
- **Ganhos:** partilhas, a parte estimada no próximo bloco do pool e os blocos pagos (lidos do nó).
- **Estado da rede:** sincronização, ligações, testnet ou rede real.

Tecnologia: Qt e C++, como o PQCH Core. Reaproveita a compilação para Windows (`depends`), a
assinatura das versões, as traduções e o código da carteira. A aplicação em si fica com licença MIT;
o `pqch-pool` é um programa à parte com licença GPL-3, distribuído com o código-fonte.

### Versão 0 (já feita): modo `-minerapp` do PQCH Core

Antes do pool, a aplicação de um clique já existe dentro do `pqch-qt`, a minerar a solo:

- `pqch-qt -testnet -minerapp`, com atalho **PQCH Miner (testnet)** no menu Iniciar e no ambiente
  de trabalho (instalador Windows) e `pqch-miner.sh` / `pqch-miner.desktop` no pacote Linux;
- não pergunta a pasta de dados (usa a de origem);
- se não houver carteira, abre uma que exista no disco; se não houver nenhuma, abre a criação com o
  nome "Mining" já preenchido (palavra-passe obrigatória e as 24 palavras, como sempre);
- abre no separador **Mineração**; a opção "Start mining when PQCH opens" faz a mineração arrancar
  sozinha nas aberturas seguintes (o minerador espera pela sincronização e pelas ligações).

Quando o `pqch-pool` existir (fases A–C), o mesmo modo passa a minerar no pool em vez de a solo.

## 3. Fases

| Fase | Quando | O quê | Critério |
| --- | --- | --- | --- |
| A. Fork e cadeia principal | **feita a 20 nov. 2026** | Repositório `pqch-core/pqch-pool`; modelo de bloco, coinbase, prova de trabalho e envio de blocos com o `pqchd`; um só nó do pool em regtest | O pool encontra blocos em regtest e a coinbase paga certo |
| B. Rede do pool | **feita a 20 nov. 2026** | Cadeia de partilhas entre vários nós; PPLNS; parâmetros PQCH | Nós do pool na testnet (âncora e Kali) na mesma cadeia e a dividir os pagamentos certo |
| C. Minerador | **feita a 21 nov. 2026** | Minerador próprio com stratum | Minerar no pool a partir de outra máquina |
| D. Aplicação | **em curso (desde 21 nov. 2026)** | Aplicação de um clique, Windows e Linux; instaladores assinados | Pessoas de fora a minerar no pool da testnet com a aplicação |
| E. Testes e auditoria | fev. 2027 | Testes com mineiros de fora; o pool e a aplicação entram no âmbito da auditoria | Sem falhas graves por corrigir |

**Fase A feita (20 nov. 2026)**, em `pqch-core/pqch-pool`:

- ligação ao `pqchd` (`getblocktemplate` a cada segundo, `submitblock`, cookie); `--pqchd-check` para
  confirmar que o pool funciona com um nó;
- bloco PQCH completo: moradas P2MR, divisão PPLNS, coinbase (altura, identificador da partilha,
  extranonce, compromisso SegWit), Merkle, cabeçalho de 80 bytes;
- prova de trabalho RandomX v2 igual à do `pqchd` (vetores oficiais);
- cadeia de partilhas com as regras do P2Pool (partilha a cada 10 s, PPLNS de 2.160 partilhas, dificuldade
  que segue o ritmo). **Decisão:** é uma cadeia de partilhas própria da PQCH, e não o `side_chain.cpp` do
  P2Pool adaptado, porque esse está cosido ao formato do Monero (chaves de uso único, saídas derivadas,
  provas de merged mining). A rede entre nós, o stratum e o resto do P2Pool continuam a ser aproveitados nas
  fases B e C;
- critério cumprido: um nó do pool em regtest encontra blocos e cada coinbase, tal como ficou gravada pelo
  `pqchd`, é a divisão PPLNS exata das partilhas (verificado em Python, independente do código do pool;
  também no CI do `pqch-pool`, que compila o `pqchd` a partir deste repositório).

Encontrou-se pelo caminho uma falha no `pqchd`: o `getblocktemplate` falhava em todas as redes (coinbase
provisória `OP_TRUE`, proibida pela regra pós-quântica). Corrigido em `c31466e`, com o teste
`mining_pqch_getblocktemplate.py`. Os nós dos servidores precisam desta versão antes da fase B.

**Fase B feita (20 nov. 2026)**, com o `pqchd` 0.3.1 nas três máquinas:

- rede própria entre nós do pool (TCP, porta 37890): partilhas com o ramo de Merkle da coinbase, pedido de
  partilhas em falta, escolha do ramo com mais trabalho; cada nó confere a coinbase de cada partilha recebida;
- na testnet: a âncora escuta (1 núcleo) e o Kali minera só pelo pool (8 núcleos, modo rápido;
  `--substituir-minerador` trava o minerador direto). O server ficou com o merged mining do Monero, para
  os blocos do pool competirem com outros mineiros como na rede real;
- em 15 minutos: uma só cadeia de 150 partilhas nos dois nós, dificuldade ajustada de 1.000 para ~50.000
  (uma partilha a cada 10 s) e os blocos 21777–21782 da testnet encontrados pelos dois nós, cada um a pagar
  aos dois mineiros;
- critério cumprido: `deploy/verificar_pool_testnet.py` (independente do código do pool) deu 6 blocos em 6
  com a divisão PPLNS exata, corrido em cada nó com o seu próprio registo de partilhas. Antes, em regtest e
  no CI: dois nós e um terceiro que entra a meio, na mesma cadeia e a pagarem-se um ao outro.

**Fase C feita (21 nov. 2026):** o nó do pool serve mineradores sem nó por stratum (`--stratum`, porta 37889,
por agora só na rede local) e o mesmo programa faz de minerador (`pqch-pool --pqch-miner ip:porta morada`). Cada
minerador recebe trabalho com a sua morada na coinbase e é pago por PPLNS; o nó recalcula cada hash recebido.
Na testnet, o server (sem nó do pool, 2 núcleos, 80 H/s) minerou pelo stratum do Kali: 435 blocos do pool nos
últimos 500, todos com a divisão PPLNS exata e a morada do server paga em cada bloco recente.

**Fase D (em curso, 21 nov. 2026):** a página "Mineração" da carteira ganhou o modo **Pool**: escolhe-se o nó do
pool (na testnet, o stratum público da âncora, 187.127.89.83:37889, já preenchido) e a carteira arranca o
programa `pqch-pool --pqch-miner <nó> <morada nova da carteira>` que vem ao lado do pqch-qt, e mostra a
velocidade e as partilhas aceites. Programa separado, não código dentro da carteira: o pqch-pool é GPL-3.0 (do
P2Pool) e a carteira MIT, e se o minerador falhar a carteira não vai abaixo nem ele lhe chega às chaves. O
pqch-pool tem releases próprias (Linux e Windows, SHA256SUMS assinado com a chave de releases); a release da
carteira vai buscá-las pela versão e pelo SHA256 fixados em `contrib/pqch/pqch-pool.sha256`. Antes de abrir o
stratum à Internet, o nó ganhou limites por endereço (8 ligações, 20 novas por minuto, recusa de 10 minutos a
quem não cumprir o protocolo, 10 verificações de partilhas por segundo) e deixou de dar trabalho enquanto
sincroniza a cadeia de partilhas. A âncora deixou de minerar (créditos de CPU da Hostinger).

O pool não faz parte do consenso: pode continuar a ser melhorado depois de o código do nó congelar
(1 de março), desde que esteja pronto no dia do lançamento.

## 4. Riscos

- **Calendário apertado:** menos de quatro meses até ao lançamento, em paralelo com a calibração. Se apertar,
  a aplicação sai primeiro só para Linux e Windows 64 bits, sem modo avançado.
- **Licença GPL-3:** o `pqch-pool` tem de ser distribuído com o código-fonte. A aplicação chama-o
  como programa à parte e não fica presa à GPL.
- **Pagamentos errados** são o risco mais grave de um pool: cada nó do pool tem de verificar que a
  coinbase de cada bloco paga exatamente a janela PPLNS. Testes próprios e fuzzing do formato das
  partilhas, como foi feito para o AuxPoW.
- **Ataques à cadeia de partilhas** (partilhas falsas, retenção de blocos): as defesas do P2Pool já
  existem e têm anos de uso no Monero; mudam só os formatos.
- **Mineiros com pouca memória:** o modo rápido do RandomX precisa de ~2,1 GB. A aplicação usa o
  modo leve abaixo de 4 GB de memória livre.
