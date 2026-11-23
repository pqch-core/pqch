# Dificuldade inicial do génesis da rede principal (simulação, 23 nov. 2026)

Decisão (23 nov. 2026): uma dificuldade inicial baixa, com o ASERT a ajustar nas primeiras horas.
Este documento mede o que isso custa e fixa a forma de criar o génesis. A simulação está em
`genesis_simulation.py`, usa os valores do código e corre em 3 s.

## Como o ASERT trata o arranque

O ASERT do PQCH é absoluto e está ancorado no génesis. O alvo de cada bloco depende só de três
coisas:

- o `nBits` do génesis;
- a hora do génesis;
- a altura do bloco.

A dificuldade duplica por cada **240 blocos** (uma meia-vida de 4 h) que a cadeia avance sobre o
calendário de um bloco por minuto. Se o hashrate do arranque for **R vezes** o do equilíbrio do
génesis, saem a mais cerca de **240 × log2(R)** blocos. Quase todos saem nas primeiras horas, e o
crescimento é só logarítmico.

O génesis provisório (`nBits 0x1e7fffff`) já está no `powLimit`, o alvo mais fácil possível. O seu
hashrate de equilíbrio é 2^17 / 60 ≈ **2,2 kH/s**, menos do que um computador comum de 8 núcleos.

## Resultados (mediana de 40 corridas)

| Génesis (hashrate de equilíbrio) | Hashrate no arranque | Blocos na 1.ª hora | Blocos a mais em 24 h | PQCH a mais | % da emissão | Duração da rajada (90 %) |
|---|---|---|---|---|---|---|
| 2,2 kH/s (`powLimit`) | 20 kH/s | 346 | +762 | 5.330 | 0,009 % | 8,9 h |
| 2,2 kH/s | 200 kH/s | 1.006 | +1.555 | 10.885 | 0,018 % | 5,8 h |
| 2,2 kH/s | 2 MH/s | 1.784 | +2.355 | 16.485 | 0,028 % | 4,1 h |
| 2,2 kH/s | 20 MH/s (merged mining forte) | 2.582 | +3.155 | 22.085 | 0,037 % | 3,1 h |
| 17,5 kH/s (`0x1e0fffff`) | 200 kH/s | 400 | +838 | 5.866 | 0,010 % | 8,6 h |
| 17,5 kH/s (`0x1e0fffff`) | 2 kH/s (arranque fraco) | 7 | −713 | 0 | 0 % | **~20 h lenta**, esperas de 29 min |
| igual ao hashrate | igual | 60 | −6 | 0 | 0 % | — |
| 10× acima do hashrate | — | 6 | −750 | 0 | 0 % | **~21 h lenta**, esperas de 31 min |

Com o hashrate a crescer 10× nas primeiras 24 h (mais mineradores a juntar-se), cada linha soma
cerca de +650 blocos. Uma dificuldade certa no arranque dá na mesma +648 blocos nesse cenário.

**Atraso.** Se a mineração começar D horas depois da hora escrita no génesis, a dificuldade fica
2^(D/4) vezes mais baixa, até ao `powLimit`. Com uma dificuldade igual ao hashrate:

| Atraso | Blocos a mais |
|---|---|
| 1 h | +54 |
| 4 h | +230 |
| 12 h | +675 |

## O que isto quer dizer

1. **Dificuldade baixa demais.** No total custa pouco: no pior caso realista ficam 0,04 % da emissão
   a mais. Mas as moedas concentram-se. Com 2 MH/s no arranque, saem em 4 h cerca de 16.500 PQCH,
   mais de 1,5 dias de emissão normal (10.080 PQCH por dia), para quem estiver a minerar nesse
   momento.
2. **Dificuldade alta demais.** Não cria moedas a mais, mas deixa a rede lenta durante cerca de
   um dia por cada fator de 10. É a pior impressão possível no dia do lançamento.
3. **Os dois erros não pesam o mesmo.** Cada fator de 10 a menos custa cerca de 800 blocos
   (~5.500 PQCH) numa rajada de poucas horas. Cada fator de 10 a mais custa cerca de um dia de rede
   lenta. Por isso a dificuldade deve ficar baixa, como decidido.

## Proposta

- **`nBits`: manter o `powLimit` (`0x1e7fffff`)**, salvo dados novos até fev. 2027. Não exige
  estimar o hashrate do arranque, que hoje ninguém conhece. A alternativa é `0x1e0fffff`
  (8× mais difícil, ~17,5 kH/s, uns 5 a 10 computadores comuns), que corta a rajada em cerca de 720
  blocos (~5.000 PQCH). Só vale a pena se houver sinais claros de muitos mineradores logo no
  arranque. O risco é o contrário: se aparecerem só ~2 kH/s, a rede fica lenta cerca de
  20 h.
- **Hora do génesis = hora anunciada do arranque, ao minuto.** Assim não há atraso (tabela acima).
  E como um bloco não pode ter hora mais de 10 min no futuro (`MAX_FUTURE_BLOCK_TIME`) nem anterior
  ao génesis, **ninguém consegue minerar mais de 10 min antes da hora anunciada**, nem quem cria o
  génesis.
- **Manchete do dia do lançamento na mensagem do génesis**, publicada à hora do arranque, junto com
  os binários assinados: o hash do génesis não se conhece antes.
- **Recompensa do génesis: 7 PQCH** (hoje 12,5, herdado do desenho antigo). A saída é OP_RETURN e
  não se pode gastar, mas o valor fica coerente com a emissão.
- **No anúncio**, explicar a rajada inicial: quem minerar na primeira hora ganha mais, e qualquer
  pessoa pode estar lá. Isto inclui os mineradores do P2Pool em merged mining.
