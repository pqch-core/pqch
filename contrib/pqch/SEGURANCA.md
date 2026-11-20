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
4. **Soft fork de emergência do ML-DSA.** Implementado e testado (19 nov. 2026); desenho abaixo.

## Soft fork de emergência do ML-DSA

**Para quê.** Se o ML-DSA for quebrado (um erro no algoritmo ou na implementação), quem o quebre
ainda precisa da assinatura Schnorr da mesma folha. Mas se o ML-DSA cair *e* existir um computador
quântico capaz de quebrar o Schnorr, a folha A de todas as moedas fica aberta. Este soft fork fecha-a:
a partir da ativação, só a folha B (SLH-DSA, baseada em hashes) gasta moedas.

**A regra (`SCRIPT_VERIFY_MLDSA_DISABLED`).** Com o soft fork ativo, `OP_CHECKSIGMLDSA` e
`OP_CHECKSIGMLDSAVERIFY` falham com qualquer assinatura não vazia (erro `SCRIPT_ERR_MLDSA_DISABLED`).
É um soft fork: só torna inválido o que antes era válido, por isso nós antigos continuam a seguir a
cadeia. Uma transação assinada pela folha A antes da ativação deixa de entrar em blocos.

**A ativação (BIP9, implantação `mldsaemergency`, bit 1).**

| Rede | Período | Limiar | Início | Fim |
|---|---|---|---|---|
| Principal e testnet | 1.440 blocos (~1 dia) | 1.296 (90%) | desde o génesis | nunca expira |
| Regtest | 144 blocos | 130 (90%) | desde o génesis | nunca expira |

Os nós **nunca sinalizam sozinhos** (`signal_by_default = false`): um bloco só leva o bit se o
mineiro arrancar o nó com `-signalmldsaemergency=1`. Assim a regra não pode ativar por acidente. Com
90% de um período a sinalizar, fica `locked_in`; um período depois fica `active`. Na rede principal,
do primeiro dia de sinalização à ativação vão cerca de 2 a 3 dias. Os blocos que sinalizam não geram
o aviso de «regras desconhecidas» nos nós desta versão.

**As carteiras.** Quando a regra está ativa, a carteira do PQCH Core passa a gastar pela folha de
recuperação sem nada a configurar (`send`, `sendtoaddress`, `sendall`, PSBT), e calcula a taxa para o
tamanho maior da assinatura SLH-DSA (~7,9 kB). A carteira Android ainda não muda sozinha: fica para a
próxima versão da app, com a opção de recuperação que já existe no núcleo.

**Procedimento, se for preciso usá-lo.**
1. Confirmar a falha com a equipa e com quem a reportou (canal privado, `SECURITY.md`).
2. Anunciar nos canais oficiais e no site: o que se passa, que as moedas estão seguras e que os
   mineiros devem arrancar o nó com `-signalmldsaemergency=1`.
3. Vigiar `getdeploymentinfo` → `mldsaemergency` até `active`.
4. Depois da ativação, as carteiras atualizadas continuam a funcionar; quem usar outro software
   gasta com `p2mr_recovery`.

**Testes.** `feature_pqch_mldsa_emergency.py` (regtest, 2 nós): ninguém sinaliza por defeito; com
`-signalmldsaemergency` a implantação ativa; uma transação pela folha A assinada antes da ativação é
recusada no mempool e num bloco, nos dois nós; a carteira gasta sozinha pela folha B (`send`,
`sendtoaddress`, `sendall`). E `pqch_pq_tests/p2mr_two_leaves_slhdsa_recovery`: com a regra, a folha A
falha e a folha B passa.

## Falhas encontradas pela automação

| Data | Onde | O quê | Correção |
| --- | --- | --- | --- |
| 19 nov. 2026 | Soft fork ML-DSA (mempool) | Depois da ativação, a política do mempool não sabia que o ML-DSA estava desligado: a transação passava a política e só falhava a verificação de consenso. Numa compilação normal era só recusada com "BUG!" no registo; em Debug parava o nó. | A política junta `SCRIPT_VERIFY_MLDSA_DISABLED` quando o soft fork se aplica ao bloco seguinte. |
| 19 nov. 2026 | Carteira (restauro pelas 24 palavras) | Ordem de bloqueio invertida (`cs_wallet` antes de `m_relock_mutex`): risco de bloqueio mútuo com o temporizador que volta a trancar a carteira. | Mesma ordem que o resto da carteira; o temporizador também já não tranca a carteira a meio do restauro. |
| 19 nov. 2026 | Testes | `pqch_presync_tests` falhava ao acaso (0,2%); `wallet_pqch_vault.py` dependia da velocidade do nó. | 1000 cabeçalhos (falha < 2e-7); o teste espera que as carteiras vejam a transação. |

As duas primeiras só aparecem numa compilação Debug (verificação de ordem de bloqueios e `Assume`), que é
a do trabalho noturno com ASan + UBSan. Por isso esse trabalho corre os testes funcionais em Debug.

## Triagem dos alertas do CodeQL (20 nov. 2026)

Primeira análise: 46 alertas "High", todos em código herdado, nenhum em código escrito pelo PQCH. Fechados como
"Won't fix" com o comentário "Código herdado (Bitcoin Core v32.1 / RandomX / SLH-DSA de referência), revisto em 20 nov. 2026".

| Alertas | Onde | Porque não é falha |
| --- | --- | --- |
| 32 | Bitcoin Core v32.1 (linhas iguais ao original, confirmadas pela etiqueta `v32.1`) | Caminhos vindos da configuração e da linha de comandos do próprio utilizador; ficheiros criados com `umask 0077` (definido no arranque em `common/system.cpp`); `!` em inteiros de propósito na secp256k1; tamanhos limitados antes da alocação. São avisos conhecidos e aceites no Bitcoin Core. |
| 10 | RandomX (`argon2_avx2.c`, `argon2_ssse3.c`, `randomx.cpp`) | Aritmética de ponteiros SIMD intencional; o `new` está dentro de `try`/`catch (std::exception&)`, a verificação de nulo a seguir é só redundante. |
| 2 | SLH-DSA de referência (`slh_dsa.c`) | Multiplicações de parâmetros fixos e pequenos (n ≤ 32, d ≤ 22): não há transbordo. |
| 1 | `univalue_write.cpp` | Indentação do JSON, valor pequeno. |
| 1 | `muhash.cpp` | Aritmética de 64 bits intencional na multiplicação modular. |

Regra para os próximos: um alerta novo em ficheiro com alterações PQCH (`src/pqch*`, `src/crypto/pqc` fora de `slhdsa-c`/`mldsa`,
`src/script/interpreter.cpp`, `src/wallet`, `src/node/miner.cpp`, `src/pow*`, `src/auxpow*`) é analisado antes de ser fechado.
