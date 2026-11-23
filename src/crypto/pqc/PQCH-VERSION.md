# Bibliotecas pós-quânticas da PQCH

| Pasta | Projeto | Versão | Licença | Algoritmo |
| --- | --- | --- | --- | --- |
| `mldsa-native/` | [pq-code-package/mldsa-native](https://github.com/pq-code-package/mldsa-native) | v2.0.0 (commit `834a90d5`) | Apache-2.0 OR ISC OR MIT | ML-DSA (FIPS 204) |
| `slhdsa-c/` | [pq-code-package/slhdsa-c](https://github.com/pq-code-package/slhdsa-c) | commit `47451432` (sem versão publicada) | Apache-2.0 OR ISC OR MIT | SLH-DSA (FIPS 205) |

## O que foi copiado

- **mldsa-native:** a pasta `mldsa/` (código C portável e backends nativos) e a `LICENSE`. Falta o
  `mldsa_native_asm.S`, que não é usado: a compilação usa só `mldsa_native.c` com o conjunto 44, o
  prefixo `PQCH_mldsa44` e `MLD_CONFIG_NO_RANDOMIZED_API` (a PQCH fornece a aleatoriedade).
- **slhdsa-c:** os ficheiros `.c` e `.h` da raiz, a `LICENSE` e o `README.md`. Não foram copiados os
  testes nem as provas formais.

Nenhum ficheiro das bibliotecas foi alterado. O invólucro da PQCH é `pqc.h`/`pqc.cpp` e a compilação
está em `src/crypto/CMakeLists.txt`.

## Antes do lançamento

- O slhdsa-c ainda se declara "work in progress". Fixar uma versão publicada (ou rever este commit) e
  incluí-la na auditoria externa.
- **Feito para a slhdsa-c (23 nov. 2026):** os vetores ACVP do NIST (FIPS 205, ACVP-Server commit
  `975de31`) do conjunto SLH-DSA-SHA2-128s, o da chave de recuperação, passam todos contra este commit:
  10 de geração de chaves, 28 de assinatura (determinística e com aleatoriedade adicional, interfaces
  "external" com contexto e "internal") e 28 de verificação (4 válidas e 24 alteradas: R, SIGFORS,
  SIGHT, mensagem, assinatura curta ou longa). Teste `pqch_slhdsa_acvp_tests`, ficheiro gerado por
  `contrib/pqch/acvp_slhdsa.py`. A 23 nov. o projeto continua sem versão publicada e o commit
  `47451432` continua a ser o mais recente.
- **Revisão da slhdsa-c (23 nov. 2026), commit `47451432`** (continua a ser o `main`, sem etiquetas
  de versão):
  - **Tempo constante:** sem falhas. `contrib/pqch/ct/slhdsa_ct.sh` usa o modo de teste da própria
    biblioteca (`SLH_CONFIG_CT_TESTING_ENABLED`) com o valgrind: marca como secretos o SK.seed, o
    SK.prf e a aleatoriedade adicional, e reporta qualquer salto ou endereço de memória que dependa
    deles. Com o gcc 13.3 (-O0, -O2, -O3, -Os) e o clang 18.1 (-O2), a geração de chaves e a assinatura
    deram 0 erros, e todas as assinaturas verificam. Os únicos valores desclassificados pela
    biblioteca são públicos por definição: R, a chave pública FORS, as raízes XMSS intermédias e a
    chave pública. Revi cada um destes pontos no código.
  - **Verificação:** o tamanho da assinatura é conferido antes de ler (`slh_verify_internal` e
    `slh_verify_digest`) e o contexto é limitado a 255 bytes. 450 mil entradas do alvo de fuzzing
    `pqch_pq_verify` (5 min) correram sem falhas, além do fuzzing contínuo da máquina de vigilância.
  - **Só usamos** `slh_keygen_internal`, `slh_sign` e `slh_verify` com o conjunto SHA2-128s. As
    variantes SHAKE e prehash estão compiladas mas não são chamadas.
  - **Achado de baixo risco, corrigido a 23 nov. 2026:** a biblioteca não apaga da pilha as cópias
    do SK.seed e do SK.prf (`slh_var_t`) nem os valores intermédios, no fim da geração de chaves e da
    assinatura. Medido: o `slh_sign` usa cerca de 5 KiB de pilha e deixa lá uma cópia do SK.seed.
    Para não alterar a biblioteca, o invólucro (`pqc.cpp`, `ClearSLHDSAStack`) apaga 64 KiB de pilha
    logo depois de cada `slh_keygen_internal` e `slh_sign`, a partir da mesma moldura. Verificado por
    `contrib/pqch/ct/slhdsa_stack.sh`, com o gcc e o clang (-O0 e -O2): sem o invólucro fica 1 cópia;
    com ele, 0. O invólucro também apaga a chave secreta e a semente expandida (`memory_cleanse`).
    A reportar ao projeto, para ser corrigido na própria biblioteca.
- Correr os vetores ACVP do NIST completos do mldsa-native. Os testes da PQCH já comparam chaves e
  assinaturas com implementações independentes (`contrib/pqch/pq_reference.py`).
- Para atualizar: substituir a pasta inteira por uma versão nova, atualizar esta tabela e voltar a
  correr `pqch_pq_tests` e `contrib/pqch/validar_fase4.sh`.
