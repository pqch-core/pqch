# RandomX (cópia integrada na PQCH)

- Origem: https://github.com/tevador/RandomX
- Versão: v2.0.1 (commit aaafe71322df6602c21a5c72937ac284724ae561)
- Licença: BSD-3-Clause (ver LICENSE)
- Conteúdo: apenas `src/` da biblioteca, sem testes, projetos Visual Studio nem documentação.
- A compilação é feita por `cmake/randomx.cmake`, que não altera as flags globais do projeto.
- A PQCH usa a variante v2 (`RANDOMX_FLAG_V2`).

Para atualizar: substituir `src/randomx/src` pela nova versão, atualizar este ficheiro e
correr `test_bitcoin --run_test=pqch_randomx_tests` (vetores oficiais).
