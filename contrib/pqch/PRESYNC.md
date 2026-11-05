# PQCH — Proteção da pré-sincronização de cabeçalhos

## O problema

Um nó novo, ou atrasado, descarrega os cabeçalhos de um par em duas passagens (`headerssync.cpp`,
herdado do Bitcoin):

1. **Pré-sincronização:** soma o trabalho da cadeia do par e guarda só compromissos, 1 bit em
   cada N cabeçalhos.
2. **Nova descarga:** só se essa cadeia tiver o trabalho mínimo é que os cabeçalhos são
   descarregados outra vez e guardados.

O Bitcoin verifica a prova de trabalho (SHA256d) de **todos** os cabeçalhos na primeira passagem.
Na PQCH isso não acontecia:

- O RandomX precisa da chave, que é o hash de um bloco anterior da cadeia, e custa ~30 ms por
  hash. Por isso a verificação estava desligada.
- O ASERT muda a dificuldade em cada bloco, e a regra de transição de dificuldade também estava
  desligada.

Assim, um par podia declarar qualquer trabalho com cabeçalhos falsos, passar a primeira passagem
sem custo e fazer o nó gastar largura de banda e CPU. A memória permanente não corria perigo: na
segunda passagem cada cabeçalho é validado por inteiro antes de ser guardado.

## O que é verificado agora em cada cabeçalho da pré-sincronização

| Verificação | Custo | Efeito |
| --- | --- | --- |
| Tempo acima da mediana dos 11 anteriores e no máximo 600 s no futuro | desprezável | o par não pode puxar a dificuldade para cima recuando os tempos |
| `nBits` igual ao alvo ASERT exato (calculado a partir do tempo e altura do pai e da âncora) | desprezável | o par não escolhe a dificuldade que declara |
| Merged mining: estrutura da prova ligada ao id do bloco, em todos os cabeçalhos | desprezável | — |
| Merged mining: regra das chaves Monero (máx. 4 novas em 4.320 blocos) | desprezável | limita as caches RandomX de 256 MiB que um par pode obrigar a criar |
| **Prova de trabalho RandomX por amostragem:** 1 cabeçalho em 64, escolhido ao acaso pelo nó, e sempre o cabeçalho que atinge o trabalho mínimo | ~30 ms por verificação | cadeias com trabalho falso são apanhadas |

**Porquê a amostragem.**
- Verificar todos os cabeçalhos custaria ~60 s por cada mensagem de 2.000.
- Com 1 em 64 são ~31 verificações, ~1 s por mensagem.
- O par não sabe quais são verificados. Uma cadeia com 400 cabeçalhos falsos escapa com
  probabilidade (63/64)^400 < 0,2%.
- Para ter 50% de hipóteses de passar, o par só pode falsificar ~1% dos cabeçalhos. Ou seja, tem
  de fazer quase todo o trabalho que declara.

**A chave RandomX** de um cabeçalho é o hash do bloco na altura de chave:
- até ao ponto de partida, vem do índice de blocos;
- acima dele, vem dos cabeçalhos do próprio par, guardados de 4.096 em 4.096 blocos.

## Testes

`src/test/pqch_presync_tests.cpp`, 5 casos:

- Uma cadeia honesta de 40 cabeçalhos é aceite.
- Tempos abaixo da mediana ou no futuro são recusados.
- Proof of work falsa é recusada, verificando todos os cabeçalhos ou por amostragem (400 falsos).
- O cabeçalho que atinge o trabalho mínimo é sempre verificado.
- Cabeçalhos minerados em conjunto: uma prova partida é recusada; a 5.ª chave Monero nova numa
  janela é recusada.
- O alvo ASERT exato é verificado com os parâmetros da rede principal (um desvio de ±1 é recusado).

## Pendente

- **Custo da validação completa na sincronização inicial.** Na segunda passagem, cada cabeçalho
  custa uma verificação RandomX (~30 ms). Um milhão de cabeçalhos são ~8 h. Hipótese: usar o modo
  rápido do RandomX (conjunto de dados de 2 GiB, hash muito mais rápido) durante a sincronização
  inicial, se houver memória.
- Uma chave Monero nova custa a construção de uma cache (~2 s nesta máquina) antes de se
  verificar o trabalho. A regra das chaves limita isto a 4 por janela, por par.
