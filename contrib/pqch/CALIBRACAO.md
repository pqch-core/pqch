# Calibração na testnet pública (etapa 4, 17 nov. 2026 a fev. 2027)

Quatro parâmetros de consenso foram escolhidos antes de haver uma rede a sério. A testnet pública
serve para os confirmar ou mudar antes de congelar o código (1 de março de 2027). Este documento diz
o que se mede, como, e o que decide cada valor.

| Parâmetro | Valor atual | Onde |
| --- | --- | --- |
| Meia-vida do ASERT | 4 h (240 blocos) | `nASERTHalfLife`, `chainparams.cpp` |
| Limite de reorganização | 60 blocos | `-maxreorgdepth`, whitepaper secção 16 |
| Janela das chaves Monero | no máximo 4 chaves novas em 4.320 blocos (3 dias) | `CheckAuxSeed`, `FASE5.md` |
| Tamanho do `tx_extra` na prova | até 512 bytes | `FASE5.md` |

## 1. O que as simulações já dizem

### ASERT (`asert_simulation.py`)

Hashrate constante (20 corridas de 50.000 blocos):

| Meia-vida | Tempo médio (s) | Variação da dificuldade | 99% dos blocos em menos de (s) |
| --- | --- | --- | --- |
| 2 h | 60,0 | 5,4% | 278 |
| **4 h** | 60,0 | 3,7% | 277 |
| 8 h | 60,0 | 2,6% | 277 |
| 1 dia | 60,0 | 1,4% | 277 |

Choques de hashrate (o hashrate cai ou sobe 10× de repente):

| Meia-vida | Queda 10×: recuperação (h) | Queda 10×: blocos em falta em 24 h | Queda 10×: maior espera (min) | Subida 10×: recuperação (h) | Subida 10×: blocos a mais em 24 h |
| --- | --- | --- | --- | --- | --- |
| 2 h | 12,6 | 400 | 28 | 6,4 | 397 |
| **4 h** | 24,7 | 754 | 33 | 12,6 | 788 |
| 8 h | 50,0 | 1.072 | 40 | 25,8 | 1.518 |
| 1 dia | 153,6 | 1.244 | 47 | 78,5 | 3.557 |

Leitura: 4 horas é um meio-termo. Com 2 horas a rede recupera em metade do tempo, mas a dificuldade
oscila mais. O que a testnet tem de mostrar é se as subidas e descidas reais de hashrate (pools de
Monero a entrar e a sair do merged mining) se parecem com estes choques.

### Ataques de maioria e o limite de 60 blocos (`ataque_simulation.py`)

Corrida entre a cadeia honesta e a cadeia privada de um atacante durante 24 h, 20.000 tentativas
por caso. Conta o trabalho, não o número de blocos: com o ASERT o trabalho acumulado por segundo é o
hashrate, por isso falsificar as horas dos blocos não dá vantagem ao atacante.

| Hashrate do atacante | Confirmações | Reverte sem limite | Reverte com limite de 60 | Parte a rede |
| --- | --- | --- | --- | --- |
| 30% | 6 | 8,6% | 8,6% | 0% |
| 30% | 30 | 0,09% | 0,09% | 0% |
| 40% | 6 | 37,4% | 37,2% | 0,14% |
| 40% | 30 | 9,8% | 9,5% | 0,33% |
| 40% | 61 | 2,0% | **0%** | 2,0% |
| 51% | 6 | 99,5% | 88,8% | 10,7% |
| 51% | 30 | 99,1% | 77,0% | 22,1% |
| 51% | 61 | 98,6% | **0%** | 98,6% |

Leitura:

- Abaixo de 30% do hashrate, 30 confirmações (meia hora) bastam na prática.
- O limite de 60 blocos faz o que promete: um pagamento com **61 ou mais confirmações (pouco mais de
  uma hora)** nunca é revertido para um nó já sincronizado, mesmo com 51% do hashrate.
- O preço é outro risco: um atacante com mais de metade do hashrate que publique uma cadeia mais
  pesada depois dos 60 blocos **parte a rede**. Os nós sincronizados ficam na cadeia honesta e os
  nós novos podem seguir a do atacante. A defesa contra isso não é o limite: é ter muito hashrate
  honesto (merged mining e o pool descentralizado da etapa 5) e um aviso claro no nó quando recusa
  uma cadeia mais pesada (`warning.h`, já existe).
- Recomendação a confirmar na testnet: manter 60 e publicar a regra «montantes grandes: esperar 61
  confirmações».

### Janela das chaves Monero (análise)

No Monero a chave do RandomX muda a cada 2.048 blocos de 2 minutos, ou seja a cada **2,84 dias**. Uma
janela de 4.320 blocos PQCH dura 3 dias se a rede estiver ao ritmo normal, e por isso apanha no
máximo 2 mudanças de chave: até **3 chaves** distintas, contra um limite de 4.

Se o hashrate cair, a mesma janela de 4.320 blocos dura mais tempo e apanha mais mudanças de chave.
O limite de 4 aguenta enquanto a janela durar menos de **5,7 dias** (o dobro de 2,84), ou seja,
enquanto a rede produzir em média mais de metade dos blocos previstos durante 3 dias. Com a
meia-vida de 4 horas, uma queda de 10× custa ~750 blocos no primeiro dia e depois recupera: fica
longe desse limite. Uma rede quase parada durante dias recusaria blocos minerados em conjunto
legítimos; nesse caso continuam a valer os blocos minerados diretamente (sem prova Monero).

A testnet confirma isto com dados reais (`chave_monero` em `blocos.csv`, abaixo) quando houver
blocos minerados em conjunto durante semanas seguidas.

## 2. Dados da testnet

A máquina de vigilância (`servidor-testes/README.md`) guarda de hora a hora, sem endereços IP:

- `blocos.csv`: altura, hash, hora, intervalo, dificuldade, se o bloco foi minerado em conjunto com o
  Monero e a chave Monero;
- `forks.csv`: cadeias concorrentes que o nó viu e o comprimento do ramo;
- `calibracao.txt`: o resumo (intervalo médio e percentil 99 nas últimas 24 h, blocos minerados em
  conjunto, chaves Monero na janela, maior ramo concorrente), mostrado pelo `pqch-estado`.

## 3. Critérios de decisão

| Parâmetro | Fica como está se… | Muda se… |
| --- | --- | --- |
| Meia-vida | o intervalo médio por semana fica entre 55 e 65 s e nenhuma queda real de hashrate deixa a rede mais de 2 h abaixo de metade do ritmo | as quedas reais são frequentes e lentas a recuperar (→ 2 h) ou a dificuldade oscila com o hash switching (→ 8 h) |
| Limite de reorganização | o maior ramo concorrente visto é muito menor que 60 (esperado: 1 a 3 blocos) | aparecerem ramos naturais acima de 20 blocos (rede mal ligada): rever a ligação entre nós antes de mexer no limite |
| Janela das chaves | o máximo de chaves Monero por janela fica em 3 ou menos | aparece uma 4.ª chave legítima numa janela: aumentar o limite ou passar a janela a tempo |
| `tx_extra` | as coinbases reais do P2Pool (rede principal) cabem em 512 B com folga | uma coinbase real se aproxima de 512 B: subir o limite |

## 4. Merged mining com o P2Pool real

Objetivo: minerar blocos da testnet PQCH em conjunto com o **Monero da rede principal**, através do
P2Pool (rede `mini`), com as versões que os mineiros usam de facto. Confirma o formato das provas, o
tamanho real do `tx_extra` e o comportamento nas mudanças de chave Monero.

Máquina: o **server** (i7-7600U, Ubuntu 24.04), com 125 GB do SSD interno só para o Monero. Os testes de 9 nov. foram na testnet e no stressnet do Monero; este é o primeiro na rede
principal.

### Passos

1. **Disco.** `/srv/monero`, só para o Monero (ver o passo 2). Nada da PQCH vai para esse disco.
2. **Nó Monero podado** (instalado a 18 nov. 2026, v0.18.5.4). Binário oficial conferido: assinatura
   do `hashes.txt` pela chave do binaryFate (`81AC 591F E9C4 B65C 5806 AFC3 F0AF 4D46 2A0B DF92`) e
   SHA256 igual ao da lista. Corre como serviço `monerod`, utilizador `monero` sem shell, dados em
   `/srv/monero`, um volume LVM de 125 GB no SSD interno do server (`ubuntu-vg/monero`, ext4); o
   serviço tem `RequiresMountsFor`. Primeiro tentou-se um SSD USB «2 TB» genérico: era contrafeito
   (erros de checksum do ext4 de 16 em 16 GB logo ao formatar, escritas encravadas) e foi retirado;
   um segundo SSD (Kingston) numa caixa USB 2.0 caía a cada 2 minutos. Um disco externo para o
   Monero tem de ser testado antes (`f3probe`) e ligado por USB 3 ou SATA:
   ```
   /opt/monero/monerod --non-interactive --data-dir /srv/monero --prune-blockchain --sync-pruned-blocks \
     --zmq-pub tcp://127.0.0.1:18083 --out-peers 32 --in-peers 0 --no-igd \
     --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18081 --log-file /srv/monero/monerod.log
   ```
   A primeira sincronização leva dias. A RPC do Monero fica só local, como a da PQCH.
3. **Nó PQCH com a API de merged mining.** No `pqch.conf` do minerador do server
   (`/var/lib/pqch-minerador/pqch.conf`), na secção `[test]`, acrescentar `mergemining=1`. A API fica
   em `http://127.0.0.1:39332/json_rpc`, só local.
4. **P2Pool (rede mini)** da versão oficial, com a assinatura conferida, a receber o Monero numa
   carteira Monero do projeto e a PQCH de teste na carteira do minerador:
   ```
   p2pool --mini --host 127.0.0.1 --wallet <endereço Monero do projeto> \
     --merge-mine 127.0.0.1:39332 <endereço PQCH de teste do minerador>
   ```
5. **Minerador de Monero.** `xmrig` ligado ao P2Pool local (`127.0.0.1:3333`), com 2 threads. Durante
   o teste, parar o minerador direto da PQCH (`systemctl stop pqch-minerar-testnet`) para não dividir o
   processador.
6. **O que registar:** cada `merge_mining_submit_solution ...: accepted` no registo do `pqchd`; o
   `tx_extra` de cada prova (`getblockheader <hash>` mostra o `auxpow`); as mudanças de chave
   Monero; quantos shares do P2Pool deram blocos PQCH.

Cuidados: nenhuma porta nova aberta no router (o P2Pool e o Monero só fazem ligações de saída com
`--in-peers 0`); a carteira Monero recebe valores reais, por isso a semente dessa carteira fica no
Bitwarden e nunca no server.

## 5. Calendário

| Quando | O quê |
| --- | --- |
| 17 nov. – dez. 2026 | Recolha de dados com a mineração da equipa; primeiros nós de fora |
| dez. 2026 | Merged mining com o P2Pool real (assim que o disco estiver no server e o Monero sincronizado) |
| jan. 2027 | Testes de hashrate na testnet: desligar e ligar mineradores (queda e subida reais), hash switching |
| até 15 fev. 2027 | Decisão final dos quatro parâmetros, com os dados deste documento |
| 1 mar. 2027 | Código congelado |
