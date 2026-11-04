# PQCH — Fase 3: prova de trabalho RandomX

A prova de trabalho passou de SHA-256d para **RandomX v2**, orientado a CPUs.

## Desenho

- **Identidade do bloco inalterada:** o id continua a ser SHA256d(cabeçalho), como no Bitcoin.
  Índices, carteiras, exploradores e Lightning não mudam.
- **Prova de trabalho separada:** RandomX_v2(chave, cabeçalho de 80 bytes) ≤ alvo(nBits).
- **Chave (seed):** hash de um bloco anterior. Muda a cada 4.096 blocos (~2,8 dias) com 64 blocos de
  atraso, a regra do Monero com constantes da PQCH. Até ao bloco 4.160 a chave é o génesis.
- **Verificação contextual:** como a chave depende da cadeia, a PoW é verificada em
  `ContextualCheckBlockHeader` (depois da verificação barata do nBits). Os pontos onde o Bitcoin a
  verificava sem contexto foram ajustados:
  - `CheckBlockHeader` e o pré-teste `HasValidProofOfWork` deixam de a verificar em cadeias RandomX;
    os cabeçalhos recebidos são verificados um a um em `AcceptBlockHeader` e o par que mente é castigado.
  - O carregamento do índice e a leitura de blocos do disco não voltam a calcular o RandomX
    (já foi verificado à entrada; a integridade continua garantida pelo hash do bloco).
- **Cache:** modo leve (256 MiB), com as duas chaves mais recentes em memória.
- **Mineração:** os RPCs `generate*` usam RandomX; `-randomxfastmining` ativa o modo rápido
  (2 GiB, conjunto de dados construído uma vez por chave). O `getblocktemplate` inclui
  `randomx_seed_height` e `randomx_seed_hash` para mineradores externos.

## Limite de dificuldade da rede principal

O limite do Bitcoin (2^224) exigiria ~4,3 mil milhões de hashes por bloco, impossível com CPUs.
O novo limite é **2^239 − 1** (nBits `0x1e7fffff`): ~131.072 hashes por bloco, ou seja, uma rede com
~2,2 kH/s produz blocos de 1 minuto. É o maior valor para o qual a aritmética de 256 bits do ASERT
não transborda (2^239 × 2^17 = 2^256); a verificação da implementação da Bitcoin Cash (2^224) foi
alargada em conformidade. O génesis provisório da rede principal usa este alvo e deixou de ser o do Bitcoin.

## Biblioteca

RandomX v2.0.1 (tevador/RandomX, BSD-3-Clause) em `src/randomx`, compilada por
`cmake/randomx.cmake` sem alterar as flags globais do projeto (o CMake original do RandomX altera-as).

## Validação

- **Vetores oficiais RandomX v2** (os da própria biblioteca): passam.
- **Calendário da chave** e **hash da chave** a partir da cadeia: passam.
- **Verificação de PoW:** um cabeçalho passa se e só se o hash RandomX cumpre o alvo.
- **ASERT, emissão e testes de PoW herdados:** continuam a passar (vetores do ASERT regenerados para o novo limite).
- **Regtest, 3 nós:** 200 blocos minerados no nó A em ~20 s; B e C receberam-nos pela rede P2P,
  validaram-nos e ficaram na mesma ponta; o `getblocktemplate` indica a chave correta.
- **Rede principal provisória, isolada, dificuldade real:** ver resultado em `validar_fase3.sh`.

## Desempenho medido (este servidor: Intel Xeon 2,8 GHz virtual, 2 núcleos)

| Operação | Resultado |
| --- | --- |
| Hash em modo leve (verificação), JIT | ~33 ms (benchmark oficial); ~45 ms dentro do nó |
| Hash em modo leve, sem JIT (interpretador) | ~263 ms |
| Mineração em modo rápido, 2 threads | ~465 H/s |
| Inicialização da cache leve (nova chave) | ~0,5 s |

**Consequência para a sincronização inicial:** a 1 bloco por minuto, um ano de cadeia tem 525.960
blocos. A ~40 ms por verificação, são ~6 horas de CPU por ano de cadeia num único núcleo deste
servidor (menos num computador de secretária moderno). Mitigações a avaliar na fase 4/6:
verificação dos cabeçalhos em paralelo; modo rápido durante a sincronização inicial (~4 ms por hash);
e, como faz o Monero, saltar a PoW de blocos antigos já cobertos por um bloco de referência
(semelhante ao `assumevalid`).

## Pendente

- Testes unitários e funcionais herdados que mineram blocos com SHA-256 (vários ficheiros de teste).
- Merged mining com o Monero (AuxPoW): fase 5.
- Proteção da pré-sincronização de cabeçalhos com PoW RandomX (custo de ~40 ms por cabeçalho falso).
