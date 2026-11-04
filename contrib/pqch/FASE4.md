# PQCH — Fase 4: regras de rede e assinaturas pós-quânticas

## Regras de rede

| Regra | Valor | Onde |
| --- | --- | --- |
| Tolerância de tempo futuro | 600 s (Bitcoin: 7.200 s) | `src/chain.h` |
| Peso máximo do bloco | 2.000.000 (Bitcoin: 4.000.000) | `src/consensus/consensus.h` |
| Maturidade da coinbase | 720 blocos (~12 h) na rede principal; 100 no regtest | `Consensus::Params::nCoinbaseMaturity` |
| Limite de reorganização | 60 blocos, depois de sincronizado | `CChainParams::MaxReorgDepth()`, `-maxreorgdepth` |

**Limite de reorganização.** Um nó sincronizado recusa mudar para uma cadeia que obrigue a desfazer
mais de 60 blocos da sua, mesmo com mais trabalho. Os blocos recusados ficam guardados e **não** são
marcados inválidos. O nó regista um aviso e mostra-o em `getblockchaininfo`.

Para seguir essa cadeia mesmo assim (reconciliação manual depois de uma partição da rede), o
operador faz `invalidateblock` do primeiro bloco do seu ramo. Durante a sincronização inicial ganha
sempre a cadeia com mais trabalho. `-maxreorgdepth=0` desativa o limite.

## Assinaturas pós-quânticas (whitepaper, secção 21)

**Bibliotecas** (em `src/crypto/pqc/`, compiladas à parte com os seus próprios avisos):

- **mldsa-native v2.0.0** (ML-DSA, FIPS 204), conjunto 44. Licença Apache-2.0, ISC ou MIT.
- **slhdsa-c**, commit `47451432` (SLH-DSA, FIPS 205). Licença Apache-2.0, ISC ou MIT. Ainda não tem
  versão estável: fixar uma versão e auditá-la antes do lançamento.
- O invólucro `pqc.h` aceita só tamanhos exatos de chave e assinatura.

**Saída P2MR** (testemunho versão 1, 32 bytes): é a raiz de Merkle da árvore de scripts, como no
BIP 360. Não há chave pública na saída nem caminho por chave. O bloco de controlo tem um byte (versão
da folha com o bit baixo a 1) e depois o caminho de Merkle.

**Folha PQ-script (versão 0xc2):** tapscript com quatro opcodes novos.

| Opcode | Valor | Chave | Assinatura |
| --- | --- | --- | --- |
| `OP_CHECKSIGMLDSA` / `…VERIFY` | 0xc0 / 0xc1 | 1.312 B | 2.420 B (+1 byte opcional de tipo de hash) |
| `OP_CHECKSIGSLHDSA` / `…VERIFY` | 0xc2 / 0xc3 | 32 B | 7.856 B (+1) |
| reservados para FN-DSA | 0xc4 / 0xc5 | — | continuam OP_SUCCESS (soft fork futuro) |

- **Elementos da pilha:** até 8.192 bytes nesta folha.
- **Mensagem assinada:** o hash de assinatura do BIP 341/342. O campo `key_version` é 1 para ML-DSA e
  2 para SLH-DSA, por isso o mesmo hash nunca serve dois algoritmos.
- **Contexto FIPS:** `PQCH/tx/v1`.
- **Versões de folha:** 0xc0 (tapscript só com Schnorr) é recusada em P2MR. As outras versões ficam
  reservadas para soft forks.

**Regra de raiz 1 (saídas), desde o génesis e incluindo a coinbase:** só são válidas P2MR, OP_RETURN,
pay-to-anchor e versões de testemunho 2–16. P2PK, P2PKH, P2SH, scripts nus e versão 0 são inválidos.
O pay-to-anchor não tem chave e serve para subir taxas (Lightning); foi acrescentado à lista do
whitepaper.

**Regra de raiz 2 (scripts):** numa folha que verifique uma assinatura Schnorr não vazia, tem de ser
verificada também pelo menos uma assinatura pós-quântica. Scripts só com hashlocks e timelocks
continuam permitidos.

**Orçamento de validação:** cada entrada pode gastar o tamanho do seu testemunho + 50 unidades, como
no tapscript. Cada verificação custa um número de unidades proporcional ao tempo medido neste
servidor:

| Verificação | Tempo medido | Custo (unidades) |
| --- | --- | --- |
| Schnorr | 40 µs | 50 |
| ML-DSA-44 | 134 µs | 200 |
| SLH-DSA-128s | 565 µs | 800 |

Fica no máximo ~0,8 µs por unidade de orçamento, e o orçamento de um bloco não passa do seu peso. No
pior caso, um bloco de 2.000.000 WU custa ~1,6 s de CPU a validar num núcleo, em vez dos ~3,2 s de um
bloco do Bitcoin. Isto cumpre o limite por bloco do whitepaper sem um contador novo.

**Ativação:** é o soft fork enterrado `pqrules` (altura 0 na rede principal e no regtest), visível em
`getdeploymentinfo`.

Os testes unitários herdados do Bitcoin usam `-testactivationheight=pqrules@…` para continuar a
testar o resto do código com scripts antigos. Os testes da PQCH correm com as regras reais.

## Validação

**Testes unitários da PQCH** (`pqch_pq_tests`, `pqch_network_tests`): passam.

- **Testes diferenciais:** as chaves e assinaturas determinísticas de ML-DSA-44 e SLH-DSA-128s são
  idênticas, byte a byte, às de implementações independentes (dilithium-py 1.5.1 e slh-dsa 0.2.5;
  `contrib/pqch/pq_reference.py`).
- **Verificação estrita:** rejeita tamanhos errados, bits trocados, outra mensagem, outro contexto,
  outra chave e dicas ML-DSA não canónicas.
- **P2MR:**
  - gasto ML-DSA, híbrido ML-DSA + Schnorr e pela folha de recuperação SLH-DSA;
  - recusados: caminho por chave, bloco de controlo inválido, folha 0xc0 e assinatura de outra folha;
  - o tipo de hash e o `key_version` entram no hash assinado;
  - versões de folha e opcodes FN-DSA continuam reservados para soft forks.
- **Regra 2:** uma folha só com Schnorr é recusada; uma folha só com hashlock é aceite.
- **Orçamento de validação:** 40 verificações ML-DSA repetidas esgotam-no.
- **Regra 1:** P2PK, P2PKH, P2SH, versão 0 e scripts nus são recusados, num bloco (também na coinbase)
  e no mempool. Uma cadeia de 100 blocos minerada para P2MR gasta a sua coinbase com ML-DSA, pelo
  mempool, até ficar num bloco.
- **Regras de rede:**
  - maturidade de 720 blocos (719 recusado, 720 aceite);
  - cabeçalho 600 s no futuro aceite, 601 s recusado;
  - reorganização de 60 blocos aceite, de 61 recusada, e depois reconciliada com `invalidateblock`.

**Validação em regtest** (`contrib/pqch/validar_fase4.sh`, 6 nós): passa.

- Recompensa para P2WPKH recusada.
- Transações assinadas por implementações independentes em Python e validadas pelo nó:
  - híbrida: 4.254 WU (1.064 vB);
  - SLH-DSA: 8.307 WU (2.077 vB);
  - só ML-DSA.
- Um segundo nó recebe e valida tudo pela rede.
- Assinatura falsificada e pagamento para saída antiga recusados.
- Reorganização de 60 blocos seguida; de 61 recusada, com a outra cadeia guardada como `valid-headers`
  e o aviso em `getblockchaininfo`.

**Testes herdados do Bitcoin Core:** as 139 suites passam, corridas uma a uma (ver "Limpeza dos testes
herdados" abaixo).

## Limpeza dos testes herdados

Corridas uma a uma, 121 das 139 suites passavam sem alterações. As outras 18 falhavam por diferenças
esperadas da PQCH e foram adaptadas, sem enfraquecer o que testam:

| Causa | Suites | Correção |
| --- | --- | --- |
| Génesis do regtest recente (os nós não entravam em sincronização inicial) | várias, em cascata | Génesis do regtest com a data do regtest do Bitcoin (2011); novo hash da cadeia de teste de 100 blocos |
| Scripts antigos (P2PK, P2WPKH, Taproot) | todas as que mineram | Regras pós-quânticas fora de alcance só nos testes herdados (`TestOpts::legacy_bitcoin_scripts`) |
| Recompensa de 12,5 em vez de 50 | `miner_tests`, `txpackage_tests`, `txdownload_tests` | Valores divididos por 4 |
| Prova de trabalho RandomX | `miner_tests`, `validation_block_tests`, `peerman_tests`, `blockmanager_tests` | Mineração com `CheckBlockProofOfWork`; `miner_tests` passa do principal para regtest |
| Prefixos de endereço, porta, unidade | `key_io`, `descriptor`, `script_standard`, `net_peer_connection`, `amount` | Vetores convertidos para `pqc`/`pqcrt`, porta 29333, PQCH/quantum |
| Magia de rede nos vetores BIP324 | `bip324_tests` | O teste passa a magia do Bitcoin explicitamente |
| `MAX_MONEY` de 58,9 milhões | `transaction_tests` | Vetores "MAX_MONEY + 1" com o novo limite |
| Snapshot UTXO do regtest | `validation_*` | Novo snapshot de teste à altura 110 |
| Blocos de 1 minuto | `denialofservice_tests` | Verificação de ponta parada a cada 150 s (era 10 min) |
| Trabalho mínimo zero numa cadeia nova | `validation_chainstatemanager_tests` | Caso "trabalho insuficiente" omitido |
| P2MR muda o significado da versão 1 | `txvalidationcache_tests` | P2MR fora das flags aleatórias desse teste |
| CSV ativo desde o bloco 1 | `miner_tests` | Modelo com transação não final recusado, como deve |

## Pendente

- Testes funcionais em Python (`test/functional`): ainda assumem o Bitcoin (recompensa de 50,
  endereços P2WPKH, snapshots às alturas 200 e 299). Adaptá-los é a próxima limpeza.

- Carteira com derivação pós-quântica e descritores P2MR (fase da carteira de referência). Até lá,
  `contrib/pqch/pq_tool.py` cria endereços e assina transações para testes.
- Rótulos de RPC e endereços: `getrawtransaction`/`decodescript` ainda chamam "witness_v1_taproot" às
  saídas P2MR.
- Soft fork de emergência que desativa o ML-DSA (whitepaper, secção 21.4): escrever e testar antes do
  lançamento.
- Fuzzing da verificação pós-quântica e testes de vetores ACVP do NIST completos.
- Pré-sincronização de cabeçalhos com ASERT e RandomX.
