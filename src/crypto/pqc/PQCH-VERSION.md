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
- Correr os vetores ACVP do NIST completos das duas bibliotecas. Os testes da PQCH já comparam chaves e
  assinaturas com implementações independentes (`contrib/pqch/pq_reference.py`).
- Para atualizar: substituir a pasta inteira por uma versão nova, atualizar esta tabela e voltar a
  correr `pqch_pq_tests` e `contrib/pqch/validar_fase4.sh`.
