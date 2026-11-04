# PQCH — Fase 2: ajuste de dificuldade ASERT

O reajuste do Bitcoin (a cada 2016 blocos) foi substituído, na rede principal, pelo ASERT
(aserti3), o algoritmo usado pela Bitcoin Cash desde novembro de 2020. A dificuldade passa a
ajustar-se em todos os blocos.

## Alterações

| Área | Ficheiro | Alteração |
| --- | --- | --- |
| Parâmetros | `src/consensus/params.h` | `fPowUseASERT`, `nASERTHalfLife`, `nASERTAnchorHeight` |
| Algoritmo | `src/pow.cpp`, `src/pow.h` | `CalculateASERT` (aritmética inteira, polinómio cúbico da especificação) e `GetNextASERTWorkRequired` |
| Rede principal | `src/kernel/chainparams.cpp` | ASERT ativo, âncora no génesis, meia-vida de 4 h (240 blocos) |
| Testes | `src/test/pqch_asert_tests.cpp` | Vetores de referência e cenários |
| Testes herdados | `src/test/pow_tests.cpp` | Testes do reajuste antigo correm com o ASERT desligado |
| Referência | `contrib/pqch/asert_reference.py` | Implementação independente em Python que gera os vetores |
| Simulação | `contrib/pqch/asert_simulation.py` | Calibração da meia-vida |

## Fórmula

alvo(n+1) = alvo(âncora) × 2^((t(n) − t(pai da âncora) − 60 × (n − h(âncora) + 1)) / meia-vida)

Com o génesis como âncora, o "pai da âncora" é tomado como génesis − 60 s. Assim o primeiro
bloco tem exatamente a dificuldade do génesis.

## Validação

- **Vetores de referência:** cerca de 460 casos gerados pela implementação Python, escrita a
  partir da especificação e independente do C++. Incluem casos de fronteira (no horário, uma
  meia-vida atrasado ou adiantado, limites de alvo) e casos aleatórios com alturas até 10 milhões.
- **Polinómio:** erro máximo de 0,0117% face a 2^x; a soma cabe sempre em 64 bits.
- **Cenários:** blocos no horário mantêm o alvo; uma meia-vida de atraso duplica-o; uma meia-vida
  de avanço reduz-o a metade; uma paragem longa leva ao limite (powLimit), nunca acima; uma
  rajada nunca leva o alvo a zero; o resultado depende só do tempo e da altura totais.

## Simulação da meia-vida (blocos de 60 s, 20 repetições por caso)

| Meia-vida | Variação da dificuldade | Queda 10x: recuperação | Queda 10x: blocos em falta em 24 h | Subida 10x: recuperação | Subida 10x: blocos a mais em 24 h | Vantagem de um oportunista |
| --- | --- | --- | --- | --- | --- | --- |
| 2 h | 5,4% | 12,6 h | 400 | 6,4 h | 397 | +0,75% |
| **4 h** | **3,7%** | **24,7 h** | **754** | **12,6 h** | **788** | **−0,06%** |
| 8 h | 2,6% | 50,0 h | 1.072 | 25,8 h | 1.518 | −0,72% |
| 1 dia | 1,4% | 153,6 h | 1.244 | 78,5 h | 3.557 | −1,08% |

Leitura: meias-vidas curtas recuperam depressa de entradas e saídas de hashrate, mas oscilam mais
e começam a dar vantagem a quem entra e sai conforme a dificuldade. Meias-vidas longas são estáveis
mas deixam a rede lenta (ou rápida demais) durante dias. 4 h (240 blocos) equivale, em número de
blocos, aos 2 dias da Bitcoin Cash (288 blocos) e fica no ponto de equilíbrio. Valor final a
confirmar na testnet pública, com 2 h como alternativa se o merged mining trouxer grandes
oscilações de hashrate.

## Pendente

- **Pré-sincronização de cabeçalhos:** com o ASERT, `PermittedDifficultyTransition` aceita qualquer
  transição na rede principal. A pré-sincronização fica protegida pelos compromissos de trabalho
  mínimo; um limite que tenha em conta os tempos dos blocos será revisto na fase 4.
- **Tempo futuro máximo** (600 s) e **mediana dos 11 blocos:** fase 4. O ASERT usa os tempos dos
  blocos, por isso estas regras limitam a manipulação da dificuldade.
- **Dificuldade inicial:** o génesis definitivo terá um alvo adequado ao hashrate esperado no lançamento.
