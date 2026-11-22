# PQCH — Fase 5: mineração conjunta (merged mining) com o Monero

## A ideia em três frases

1. Um mineiro de Monero que usa o P2Pool põe, no `tx_extra` da sua coinbase, uma etiqueta de
   merged mining (0x03) com a raiz de uma pequena árvore de Merkle. Uma das folhas é o id de um
   bloco PQCH.
2. O PoW do Monero é RandomX, tal como o da PQCH. Se o hash RandomX desse bloco Monero cumprir o
   alvo da PQCH, o mesmo trabalho prova o bloco PQCH. O bloco Monero não precisa de ser válido nem
   aceite na rede Monero.
3. O mineiro não gasta mais energia: o mesmo hash serve as duas cadeias.

Os blocos PQCH minerados diretamente (RandomX sobre o cabeçalho de 80 bytes, Fases 1–3) continuam
válidos. As duas formas convivem e somam trabalho da mesma maneira.

## Formato

**Cabeçalho.** O bit `0x100` de `nVersion` marca um cabeçalho com prova AuxPoW (`auxpow.h`). A
prova vem logo depois dos 80 bytes, na rede e no disco. **O id do bloco continua a ser o SHA256d
dos 80 bytes** e não depende da prova.

| Campo | Conteúdo |
| --- | --- |
| `parent_header` | cabeçalho do bloco Monero (≤ 96 B) |
| `seed_hash` | chave RandomX do bloco Monero |
| `tx_count`, `coinbase_branch` | número de transações Monero e caminho da coinbase na árvore delas |
| `coinbase_midstate`, `head_size` | estado do Keccak depois dos primeiros `head_size` bytes da coinbase (múltiplo de 136) |
| `coinbase_tail` | resto do prefixo da coinbase até ao `tx_extra` (< 136 B) |
| `coinbase_extra` | o `tx_extra` (≤ 512 B), com uma e uma só etiqueta 0x03 |
| `aux_branch` | caminho do id PQCH na árvore de merged mining (≤ 8 níveis, até 256 cadeias) |

**Prova compacta.** No P2Pool a coinbase paga a centenas de mineiros e pode ter dezenas de KB.
Isso seria demasiado para cada cabeçalho, por isso o cabeçalho leva só o estado intermédio do
Keccak. Uma prova típica tem algumas centenas de bytes, e a maior possível tem 1.916 B. Um
`headers` com 2.000 cabeçalhos cabe assim numa mensagem P2P (há um `static_assert` que garante
isto).

**Corpo do bloco.** O bloco leva também os bytes resumidos pelo estado intermédio
(`CAuxPowCoinbaseHead`, ≤ 256 KiB), depois das transações. Esses bytes não contam para o peso do
bloco. Ao validar o bloco, `cabeça ‖ cauda` tem de ser exatamente um prefixo canónico de coinbase
Monero v2: versão 2, um input `gen`, saídas para chaves, e acabar onde começa o `tx_extra`. Sem esta
verificação, quem tivesse o trabalho podia esconder dentro do "estado intermédio" uma segunda
etiqueta de merged mining. Assim, um único PoW Monero podia provar dois blocos PQCH diferentes. É o
problema que a Tari corrigiu no aviso GHSA-3qmx-q9pv-f3m4.

**Hashing igual ao do Monero e do P2Pool v4.** Usa Keccak-256 com o padding original (0x01), o
`tree_hash` do Monero e `get_root_from_proof`, que liga o comprimento da prova ao tamanho da
árvore. A posição de cada cadeia (slot) é `SHA256(chain_id ‖ nonce LE ‖ 'm')`, lida em LE, mod n.
`chain_id` é o id do bloco génese, e todos os hashes vão nos 32 bytes em ordem interna. O vetor
real da Tari (um template da rede principal Monero e o respetivo hashing blob) está nos testes
unitários.

## Regras de consenso novas

| Regra | Onde | Erro |
| --- | --- | --- |
| Prova ligada ao id do bloco no slot da PQCH; caminhos com o comprimento certo | `CheckAuxPowHeader` | `bad-auxpow (...)` |
| RandomX(chave Monero, hashing blob) ≤ alvo | `CheckBlockProofOfWork` | `high-hash` |
| No máximo 4 chaves Monero **novas** em cada 4.320 blocos (3 dias). Uma chave é nova se nenhum bloco da janela a introduziu | `CheckAuxSeed`, antes de qualquer RandomX | `auxpow-too-many-seeds` |
| Cabeça da coinbase confere com o estado intermédio e é um prefixo canónico | `CheckBlock` | `bad-auxpow-coinbase` (MUTATED) |
| O bloco traz a mesma prova que o cabeçalho indexado | `AcceptBlock` | `bad-auxpow-mismatch` (MUTATED) |

**Porquê o limite de chaves.** Cada chave RandomX nova custa a quem valida uma cache de 256 MiB,
cerca de 0,5 s a construir. Sem limite, um atacante podia escolher uma chave diferente em cada
bloco. Com o limite há 4 chaves em 3 dias, e o Monero muda de chave a cada 2.048 blocos (~2,8 dias).
Os valores são provisórios e devem ser calibrados no testnet.

**Porquê MUTATED.** Como o id não depende da prova, um par que envie dados errados (sem a cabeça,
ou com outra prova) não pode fazer marcar como inválido um bloco que é bom. Esses erros contam como
"dados corrompidos" e o bloco certo continua a ser aceite quando chega.

**Outros ajustes:**

- O aviso de "versionbits desconhecidos" ignora o bit 8.
- O `txindex` calcula o deslocamento com o tamanho real do cabeçalho.
- Os blocos compactos (BIP152) levam a cabeça da coinbase.
- A pré-sincronização de cabeçalhos guarda a prova para a fase de nova descarga.
- O índice de blocos guarda a prova em disco, para servir cabeçalhos aos pares.

## API para o P2Pool

Ativa-se com `-mergemining` (desligada por omissão). Fica em `http://<rpcbind>:<rpcport>/json_rpc`
e não pede autenticação, porque o cliente do P2Pool não envia credenciais. O acesso fica limitado
por `-rpcbind` e `-rpcallowip`, que por omissão só aceitam localhost.

| Método | Parâmetros | Resposta |
| --- | --- | --- |
| `merge_mining_get_chain_id` | — | `chain_id` |
| `merge_mining_get_aux_block` | `address` (P2MR), `aux_hash`, `height`, `prev_id` | `aux_blob`, `aux_diff`, `aux_hash` |
| `merge_mining_submit_solution` | `aux_blob`, `aux_hash`, `blob`, `merkle_proof`, `path`, `seed_hash` | `status` |

- `aux_diff` é o alvo PQCH expresso como dificuldade Monero (2²⁵⁶ / alvo).
- O nó guarda os 16 últimos templates.
- Faz um template novo quando muda a ponta da cadeia ou ao fim de 30 s.
- O `path` não é usado: a posição sai do `chain_id` e do nonce da etiqueta.

**Como experimentar com o P2Pool** (na tua máquina, com `monerod` e `p2pool` v4):

```
pqchd -server -mergemining -rpcport=18766
p2pool --wallet <endereço XMR> --merge-mine 127.0.0.1:18766 <endereço PQCH P2MR>
```

O P2Pool chama primeiro `merge_mining_get_chain_id` e depois pede trabalho a cada segundo. Quando
um share cumpre `aux_diff`, submete a solução e regista
`merge_mining_submit_solution ...: accepted`.

## Testes

`src/test/pqch_auxpow_tests.cpp`, 11 casos:

- Keccak-256 do Monero, incluindo retomar a partir do estado intermédio.
- Árvore e provas do Monero para 1–40 folhas: comprimento errado e índice errado são recusados.
- Parâmetros de merged mining do P2Pool (codificação canónica estrita).
- Varints do Monero.
- O vetor real da rede principal Monero.
- Ligação da prova ao id, ao slot e à contagem de transações; duas etiquetas são recusadas.
- A cabeça da coinbase, incluindo o ataque GHSA-3qmx-q9pv-f3m4 e a mudança do ponto de corte.
- Serialização do cabeçalho e do bloco (o id não muda; cabeçalho sem prova não serializa).
- A regra das chaves.
- Ponta a ponta no regtest: o bloco minerado em conjunto é aceite, lido do disco e seguido por um
  bloco normal. Sem a cabeça, ou com outra prova, o bloco é recusado sem ficar marcado inválido.
- A API do P2Pool completa: `get_chain_id`, `get_aux_block`, `submit_solution` e `accepted`.

## Versão do RandomX na prova do Monero (0.2.1)

A prova de trabalho própria do PQCH é RandomX v2. A prova de um bloco minerado em conjunto é a do
**Monero**, que usa o RandomX original (v1) até ao seu hard fork 17 e o RandomX v2 a partir dele.
`MoneroPowHash` escolhe a versão pelo primeiro byte do hashing blob (a versão principal do bloco
Monero): v1 abaixo de 17, v2 de 17 em diante, a mesma regra do P2Pool (`HARDFORK_VERSION_RANDOMX_V2`).

Descoberto no primeiro teste real (9 nov. 2026, P2Pool v4.18.1, testnet Monero, versão 16): a 0.2.0
calculava sempre v2 e recusava todos os blocos com `high-hash`. Testes: vetores oficiais do RandomX
v1 (incluindo um hashing blob Monero) e `monero_pow_hash_version`.

## Ainda por fazer

- ~~Teste real com o P2Pool e o Monero~~ **Feito a 9 nov. 2026 (0.2.1).** Portátil com `monerod`
  v0.18.5.1 no testnet Monero (offline, `--fixed-difficulty 20000`, versão 16, RandomX v1), P2Pool
  v4.18.1 com o minerador interno e `--merge-mine` para o `pqchd` do portátil. Resultado: todos os
  `merge_mining_submit_solution` com `accepted`; blocos PQCH com `version` 536871168 (bit `0x100`);
  a âncora (VPS, nó independente) chegou ao mesmo bloco (altura 118, `b974cb14…6498441`) sem nenhum
  `bad-auxpow` nem `high-hash`. Os formatos copiados do P2Pool v4 (`merkle_proof`, `blob`) estão certos.
- ~~Testar a regra das chaves Monero~~ **Feito a 9 nov. 2026.** Cadeia Monero local com
  `--fixed-difficulty 2000` durante cerca de 38.000 blocos (várias mudanças de chave). Entraram 4
  chaves novas (`c7a525…`, `8a3797…`, `717754…`, `3c8fc3…`; 41, 42, 28 e 31 blocos aceites) e a
  5.ª foi recusada com `auxpow-too-many-seeds`. A âncora ficou no mesmo bloco (altura 249,
  `016c387e…a8a10a2`) sem nenhuma recusa: os blocos recusados nunca saíram do portátil. Nota: o
  P2Pool prepara um bloco Monero por segundo no máximo, mesmo com dificuldade mais baixa.
- Arrumação do teste (9 nov. 2026): 38.264 blocos Monero de teste removidos (`pop_blocks`, de volta à
  altura 3.126.706); testnet PQCH devolvida ao bloco 118 (`b974cb14…6498441`) com `invalidateblock`
  do bloco 119 (`e95cdf7b…cda2ed6b`) nos dois nós, para as 4 chaves de teste saírem da janela;
  `mergemining=1` retirado e mineração normal retomada (âncora e portátil de acordo no bloco 120).
- **Hard fork 17 do Monero (FCMP++, Carrot, RandomX v2): suportado desde a 0.2.3.** Primeiro teste real
  no stressnet (9 nov. 2026, versão 18, P2Pool v5.0-beta, monerod v0.19.0.0-beta.3.0): a reconstrução do
  bloco estava certa (o id do bloco bate com o do monerod), mas a 0.2.2 usava o hash RandomX v2 sem o
  **compromisso** (`randomx_calculate_commitment`), por isso todos os blocos davam `high-hash`.
  Corrigido na 0.2.3, com o bloco real como teste (`monero_stressnet_real_block`: id e hash de prova de
  trabalho iguais aos do monerod). **Teste real com a 0.2.3 passou (9 nov. 2026):** P2Pool v5.0-beta com
  o minerador interno sobre o stressnet offline (`--fixed-difficulty 20000`, versão 18); todas as
  submissões `accepted`, 20 blocos PQCH (331 → 351) com a chave Monero `48afc8fa…f2b4d`; a âncora
  (0.2.3) chegou ao mesmo bloco 350 (`ee05ddf0…820fca1a`) sem nenhum `bad-auxpow` nem `high-hash`.

## Hard fork 17 do Monero (0.2.2)

Formato do P2Pool v5.0-beta (`block_template.cpp`, `merge_mining_client.cpp`), que segue o código do
Monero. A partir da versão principal 17 de um bloco Monero:

| O quê | Até à versão 16 | A partir da 17 |
| --- | --- | --- |
| Prova de trabalho | RandomX v1 | compromisso RandomX v2: Blake2b-256(hashing blob ‖ hash RandomX v2) (0.2.3) |
| Saídas da coinbase | `txout_to_key` (0x02), `txout_to_tagged_key` (0x03) | também `txout_to_carrot_v1` (0x01): chave de 32 B, view tag de 3 B, âncora Janus cifrada de 16 B |
| Fim do blob do bloco | hashes das transações | hashes das transações, depois 1 byte (camadas da árvore FCMP++) e 32 B (raiz FCMP++) |
| Folhas da árvore de transações | coinbase, transações | coinbase, camadas (hash de zeros com a contagem no 1.º byte), raiz FCMP++, transações |
| Contagem no hashing blob | transações + 1 | transações + 1 (as duas folhas FCMP++ não contam) |

O `CAuxPow` não muda de formato: `tx_count` continua a ser a contagem do hashing blob, e
`TreeLeafCount()` soma as duas folhas FCMP++ quando a versão do cabeçalho Monero é 17 ou mais. Os
valores FCMP++ ficam dentro do `coinbase_branch`. As saídas Carrot só são aceites com cabeçalho 17
ou mais, tanto em `BuildFromMoneroBlock` como em `CheckAuxPowCoinbase`. Testes:
`monero_fcmp_pp_block_layout` (valores esperados calculados à parte do parser) e
`merged_mined_block_monero_hf17` (bloco PQCH aceite com um pai Monero 17, RandomX v2).
- `getblockheader` e `getblock` ainda não mostram os campos da prova.
- A framework de testes em Python não lê cabeçalhos com prova. Não faz falta enquanto os testes
  funcionais não minerarem em conjunto.
- Calibrar no testnet a janela de chaves (4.320 blocos / 4 chaves) e o limite do `tx_extra` (512 B).
- ~~Ataque de negação de serviço: um par pode enviar um cabeçalho com uma chave nova e trabalho
  falso.~~ Resolvido a 15 nov. 2026 (0.2.11). Medido: uma chave nova custa ~480 ms e 256 MiB (contra
  ~25 ms com a cache feita), com o `cs_main` preso, e expulsa uma das 4 caches em uso. O limite de
  chaves só cobre cadeias válidas: um cabeçalho falso passa sempre como "chave nova permitida", e as
  chaves Monero são 32 bytes à escolha de quem envia (ao contrário das chaves diretas, que vêm de
  blocos da cadeia). Defesa (`NeedsNewAuxKey` em `net_processing.cpp`): um cabeçalho que traz uma
  chave Monero que nenhum cabeçalho válido da janela usou e que não está em cache só é verificado
  quando vem de um par **de saída** (escolhido pelo nó) ou com permissão `noban`; de um par de entrada
  é ignorado, sem castigo e sem construir a cache, até um par de saída o retransmitir (os pares
  honestos retransmitem blocos válidos). Aplica-se a `headers`, `cmpctblock` e a blocos não pedidos.
  Uma chave nova legítima aparece cerca de uma vez a cada 3 dias. Teste:
  `pqch_auxpow_chain_tests/new_monero_key_only_checked_from_chosen_peers` (falha sem a defesa).
- **Correção de 22 nov. 2026 (0.3.7).** A testnet mostrou o limite dessa defesa: o servidor de casa
  minerava em conjunto com o P2Pool e o seu nó só faz ligações de saída (atrás do router, sem portas
  abertas, como quase todos os mineradores do P2Pool). Para a âncora e a VPS ele é um par de entrada,
  e ninguém mais tinha os blocos para os retransmitir: os blocos com a chave Monero nova eram
  ignorados e perdiam sempre (0 de 1.440 num dia; ramos laterais de 1 a 2 blocos a cada ~10 blocos).
  Agora, de um par de entrada sem `noban`, o nó verifica **um** cabeçalho com chave nova por minuto
  (para todo o nó), com uma cache própria e fora do `cs_main` (`PreverifyNewAuxKeyPow`): com trabalho
  falso, o par é castigado e nenhuma cache em uso é afastada; com trabalho real, a cache entra na
  lista partilhada e a validação usa o resultado sem voltar a calcular. Os outros cabeçalhos com chave
  nova nesse minuto esperam, sem castigo; um bloco compacto com chave nova faz o nó pedir os
  cabeçalhos. Custo máximo para um atacante: ~0,5 s de um núcleo e 256 MiB por minuto. O teste acima
  passou a cobrir o par de entrada (verificado uma vez por minuto) e que as caches em uso ficam.
