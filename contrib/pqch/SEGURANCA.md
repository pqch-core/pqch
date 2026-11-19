# Segurança antes do lançamento

Estado a 19 nov. 2026 dos quatro pontos de segurança do cronograma, e o que corre sozinho. O levantamento dos
ataques de mai. a nov. 2026 e da exposição do PQCH a cada um está no documento «PQCH — Ataques de mai. a nov. 2026
e a nossa exposição».

## O que corre automaticamente

| Verificação | Quando | Onde |
|---|---|---|
| Testes unitários com ASan + UBSan | cada push para `main` e cada pull request | `pqch.yml`, job `sanitizers` |
| Fuzzing de 10 alvos (AuxPoW, primitivas Monero, assinaturas PQ, descritores P2MR, BIP39, carteira), 1 min cada | cada push e pull request | `pqch.yml`, job `fuzz` |
| Testes unitários com TSan | todas as segundas-feiras | `pqch.yml`, job `tsan` |
| Análise estática CodeQL (`security-extended`) | todas as noites | `seguranca.yml`, job `codeql`; resultados no separador Security do GitHub |
| Os mesmos 10 alvos de fuzzing, 15 min cada, com o corpus guardado de noite para noite | todas as noites | `seguranca.yml`, job `fuzz-long` |
| Testes funcionais da carteira PQCH, P2MR e validação de blocos com ASan + UBSan | todas as noites | `seguranca.yml`, job `asan-functional` |
| Fuzzing contínuo (alvos PQCH 45 min, herdados 15 min, em ciclo) | sempre | máquina de vigilância (Mac), `servidor-testes/fuzz-ciclo.sh` |
| Oferta total: a soma das moedas existentes nunca passa a emissão máxima até à altura atual | de hora a hora | máquina de vigilância, `servidor-testes/vigia.sh` (alerta «OFERTA ACIMA DO MÁXIMO») |
| Entropia das 24 palavras: 4.096 sementes novas, sem repetidas, bits e palavras sem desvio | cada push (testes unitários) | `pqch_p2mr_wallet_tests/bip39_new_words_entropy` |
| Ações do GitHub fixadas por hash, atualizadas por pull request semanal | sempre | `.github/dependabot.yml` |

Uma falha em qualquer destes trabalhos é tratada como relato de segurança: não se publica o input
nem o detalhe; segue-se o `SECURITY.md`.

## Os quatro pontos

1. **Auditoria externa.** Não se automatiza: é uma pessoa de fora a ler o código novo. As
   verificações acima reduzem o risco, mas não a substituem. Para caber no orçamento, o âmbito
   mínimo é o código de consenso novo: validação AuxPoW, ASERT e verificação de assinaturas P2MR.
   As carteiras podem ser revistas depois do lançamento, porque um erro nelas não divide a rede.
2. **Recompensas por falhas.** Também não se automatiza. Falta decidir quem paga e em quê (não há
   tesouraria), e o advogado tem de ver os termos. Até lá, o relato privado no GitHub e o endereço
   community@pqch.org (assunto «security») estão abertos.
3. **Sanitizers e fuzzing.** Automatizados (tabela acima). Falta: alvos de fuzzing para o ASERT e
   para a regra das chaves RandomX no AuxPoW.
4. **Soft fork de emergência do ML-DSA.** O mecanismo ainda não existe no código. Primeiro o
   desenho (que gastos passam a exigir a folha SLH-DSA, como se ativa, a sinalização de 90%); depois
   um teste funcional em regtest que ativa o soft fork e confirma que os gastos só com ML-DSA são
   recusados e os de recuperação com SLH-DSA continuam válidos.
