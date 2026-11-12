# PQCH — Cofres para agentes

Um **cofre** guarda as moedas de um **agente**, um programa que paga sozinho (por exemplo, um
serviço de IA que compra dados ou poder de cálculo). O agente gasta as moedas do cofre quando
quiser. O **dono** pode tirá-las de lá com a sua própria chave de recuperação pós-quântica, que o
agente não tem: logo, ou só depois de um prazo escolhido ao criar o cofre.

Tudo isto usa regras que já existem no consenso desde o bloco 1 (P2MR, `OP_CHECKSEQUENCEVERIFY`).
Não há alteração de consenso: só código de carteira.

## Como é um endereço de cofre

É a mesma árvore P2MR de duas folhas de um endereço normal ([CARTEIRA.md](CARTEIRA.md)), mas a
folha B pertence ao dono e pode ter um prazo:

| Folha | Script | Quem assina |
| --- | --- | --- |
| A (quente) | `<chave x-only> OP_CHECKSIGVERIFY <chave ML-DSA-44> OP_CHECKSIGMLDSA` | o agente (semente do cofre) |
| B (recuperação) | `[<N> OP_CHECKSEQUENCEVERIFY OP_DROP] <sal> OP_DROP <chave SLH-DSA-128s> OP_CHECKSIGSLHDSA` | o dono (semente do dono) |

- `N` é o **prazo do dono** em blocos, de 0 a 65.535 (cerca de 45 dias com blocos de 60 s). Com
  `N = 0` a folha B não tem prazo.
- O prazo é **relativo** (BIP68): conta a partir do bloco em que **essa moeda** foi confirmada.
  A transação de recuperação leva `nSequence = N` nessa entrada; antes de N confirmações, o nó
  recusa-a (`non-BIP68-final`) e o script falha.
- O sal de 16 bytes muda de endereço para endereço. Assim, o hash da folha B, que fica visível
  quando se gasta pela folha A, não liga os endereços uns aos outros.

## Para que serve o prazo

| Prazo | Nome | Protege contra | Não protege contra |
| --- | --- | --- | --- |
| `older(0)` | botão de emergência | roubo, se o dono for mais rápido do que o ladrão; perda das chaves do agente | um ladrão mais rápido |
| `older(N)`, N > 0 | interruptor de homem morto | agente parado (servidor desligado, chaves perdidas, ML-DSA comprometido) | roubo: quem tiver as chaves do agente gasta de imediato |

Com N > 0, uma moeda que fique parada no cofre mais de N blocos passa a poder ser recuperada pelo
dono. O agente que quiser manter o controlo tem de mexer nas suas moedas antes disso (por exemplo,
enviá-las para um endereço novo do próprio cofre).

**O que um cofre não faz:** não limita quanto o agente gasta, nem dá ao dono uma janela para
cancelar um pagamento. Isso exigiria *covenants*, que nem o PQCH nem o Bitcoin têm. Um prazo na
folha quente também não daria essa janela, porque conta desde a chegada da moeda e não desde a
decisão de gastar. Por isso a folha A é igual à de um endereço normal.

## As sementes

- **Semente do cofre:** `HMAC-SHA512(semente da carteira do agente, "PQCH/P2MR/v2/vault" || índice)`,
  primeiros 32 bytes. As **24 palavras do agente** restauram todos os seus cofres.
- **Endereços:** `360h/{0h mainnet | 1h testes}/2h/{0h receber | 1h troco}/*h` da semente do cofre.
- **Chave do dono:** a chave SLH-DSA-128s em `360h/{0h|1h}/3h/{índice}h` da semente da carteira do
  dono. As **24 palavras do dono** restauram-na. Use um índice diferente para cada cofre.
- **O dono guarda uma cópia da semente do cofre** (não da carteira inteira do agente). Precisa dela
  para calcular os endereços: no PQCH não há xpub, por desenho. Isto quer dizer que o dono também
  conseguiria gastar pela folha A. A carteira do dono **nunca o faz sozinha**: as moedas do cofre
  não entram no saldo, nem na escolha de moedas, nem no `listunspent` do dono. Só o
  `recovervault` (ou a opção `p2mr_recovery`) lhes toca, e sempre pela folha B.

## O descritor

```
p2mr_vault(pqseed(ID)/360h/1h/2h/0h/*h,slh([DONO/360h/1h/3h/0h]CHAVE),older(N))
```

- `pqseed(ID)`: o id da semente do cofre; na forma privada, `pqprv(SEMENTE)`.
- `slh([DONO/CAMINHO]CHAVE)`: a chave SLH-DSA-128s do dono (32 bytes), com o id da semente do dono
  e o caminho. A forma privada **nunca** mostra a semente do dono.
- `older(N)`: o prazo, de 0 a 65535.

## Passo a passo (linha de comandos)

Os exemplos usam a testnet. Na interface gráfica é igual, no menu **Settings → Agent Vaults…**,
com os separadores *Vaults*, *Agent* e *Owner*.

Antes de começar:

- Use **duas carteiras diferentes**, uma do agente e outra do dono. Idealmente, em máquinas
  diferentes: a do dono não deve estar no servidor onde o agente corre.
- **Cifre a carteira do agente antes de criar o cofre.** Cifrar uma carteira sem cifra cria
  endereços novos com palavras novas, e esses deixam de ser os do cofre.

1. **Dono**, na carteira do dono (desbloqueada, se estiver cifrada):

   ```
   pqch-cli -testnet -rpcwallet=dono getvaultkey 0
   ```

   Copie o campo `key`. É público: pode ir por qualquer canal.

2. **Agente**, na carteira do agente:

   ```
   pqch-cli -testnet -rpcwallet=agente createvault "slh([...]...)" 1440
   ```

   - 1440 blocos ≈ 1 dia; use 0 para o botão de emergência.
   - Por omissão, o cofre passa a dar os endereços novos de receber e de troco desta carteira. Assim,
     o troco dos pagamentos do agente fica no cofre.
   - A resposta traz `owner_import`. **Esse texto contém a semente do cofre.** Entregue-o ao dono
     por um canal privado e apague as cópias intermédias. Na interface gráfica fica escondido: o
     botão *Copy* copia-o sem o mostrar, e *Show* mostra-o só se for preciso (não o fotografe).
   - A chave do dono tem de vir de **outra** carteira. Com a chave da própria carteira, o
     `createvault` recusa: essa carteira ficaria dona do cofre e não o poderia gastar como agente.

3. **Dono**, importa o cofre:

   ```
   pqch-cli -testnet -rpcwallet=dono importdescriptors '<owner_import>'
   ```

4. **Ver o estado**, em qualquer das carteiras:

   ```
   pqch-cli -testnet -rpcwallet=dono listvaults
   ```

   Mostra o papel (`agent` ou `owner`), o prazo, o valor (`amount`, com as moedas por confirmar, que
   aparecem também à parte em `unconfirmed`), o valor que já se pode recuperar e quantos blocos faltam
   para a próxima moeda. Uma moeda sem confirmação nunca é recuperada: espera em `waiting`.

5. **Recuperar**, na carteira do dono:

   ```
   pqch-cli -testnet -rpcwallet=dono recovervault
   ```

   - Envia numa só transação, para um endereço novo do dono (ou para o endereço indicado), todas as
     moedas do cofre que já têm confirmações suficientes. O endereço novo é sempre da carteira do
     próprio dono (`p2mr()`), nunca de um cofre; um endereço de um cofre que esta carteira possui é
     recusado, porque as moedas voltariam para o cofre.
   - As que ainda não podem ser recuperadas aparecem em `waiting`, com os blocos em falta.
   - Taxa: com `fee_rate=N` usa N quanta/vB. Sem ela usa a estimativa do nó; se o nó não tiver
     estimativa nem `-fallbackfee` (testnet calma, nó acabado de arrancar), usa 10 quanta/vB, ou
     mais se o mempool o exigir. A resposta diz a taxa paga (`fee_rate`) e de onde veio
     (`fee_source`: `given`, `estimated` ou `default`). Uma recuperação de emergência nunca falha por
     falta de estimativa.
   - Se as chaves do agente foram roubadas: recupere logo e deixe de usar esse cofre.

## Restaurar

- **Agente:** restaure a carteira com as 24 palavras e volte a chamar `createvault` com a mesma
  chave do dono, o mesmo prazo e o mesmo índice. Os descritores saem iguais. Depois corra
  `rescanblockchain`.
- **Dono:** restaure com as 24 palavras e volte a importar o `owner_import` (o dono deve guardar uma
  cópia dele em lugar seguro, porque contém a semente do cofre).

## Testes

- `src/test/pqch_pq_tests.cpp` (`p2mr_vault_timelocks`): CLTV e CSV dentro de folhas P2MR, no
  interpretador.
- `src/test/pqch_p2mr_wallet_tests.cpp` (`p2mr_vault_descriptor`): descritor (forma pública e
  privada, erros), folhas, quem assina o quê, e o limite do prazo (N assina, N − 1 não).
- `test/functional/wallet_pqch_vault.py`: o percurso completo entre duas carteiras (chave, cofre,
  importação, gasto do agente com troco no cofre, recuperação recusada antes do prazo e aceite
  depois, `older(0)`, restauro pelas palavras).
