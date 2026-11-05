# PQCH — Carteira pós-quântica

Nas redes PQCH a carteira só cria e só aceita saídas **P2MR**, endereços `pqc1p…` (`pqcrt1p…` no
regtest). Cada endereço tem duas formas de ser gasto, e nenhuma depende só de curvas elípticas.

## Como é um endereço

Cada endereço é uma árvore P2MR com duas folhas, ambas com a versão de folha 0xc2:

| Folha | Script | Testemunho do gasto | Uso |
| --- | --- | --- | --- |
| A (híbrida) | `<chave x-only> OP_CHECKSIGVERIFY <chave ML-DSA-44> OP_CHECKSIGMLDSA` | 3.876 B | todos os dias |
| B (recuperação) | `<sal> OP_DROP <chave SLH-DSA-128s> OP_CHECKSIGSLHDSA` | 7.947 B | se o ML-DSA for desativado (secção 21.4) |

Uma transação típica (1 entrada, 2 saídas) pesa ~4.430 WU. A folha B só é revelada se for usada.

## De onde vêm as chaves

As chaves saem de uma **semente pós-quântica própria** de 32 bytes. Não vêm de uma cadeia BIP32
(curva elíptica). Assim, o ficheiro da carteira não guarda nenhuma chave pública de curva elíptica
a partir da qual um computador quântico pudesse chegar à semente.

1. **Semente:** criada quando a carteira é criada, a partir de material aleatório novo
   (HMAC-SHA512 com a etiqueta `PQCH/P2MR/v2/wallet-seed`). Fica no ficheiro da carteira, cifrada
   se a carteira estiver cifrada. O id é um hash etiquetado (`PQCH/P2MR/v2/seed-id`): pode ser
   mostrado sem revelar nada.
2. **Caminho:** `360h/{0h|1h}/0h/{0h receber | 1h troco}/*h`. O `1h` do segundo passo é para as
   redes de teste.
3. **Nó de cada endereço:** `HMAC-SHA512(semente, "PQCH/P2MR/v2/node" ‖ caminho em 32 bits big
   endian)`, os primeiros 32 bytes.
4. **Chaves do nó**, por HMAC-SHA512 com etiquetas diferentes:
   - chave Schnorr (`…/schnorr`): sozinha não abre nada;
   - semente ML-DSA-44 (`…/ml-dsa-44`);
   - sal (`…/salt`).

   Um atacante quântico que tire a chave privada Schnorr da chave x-only revelada no gasto fica só
   com metade da folha A.
5. **Chave de recuperação SLH-DSA-128s:** uma só por descritor, no nó `caminho/2147483647h`
   (`…/slh-dsa-128s`).
   - **Porquê uma só:** gerar uma demora ~80 ms. Reutilizá-la é seguro, porque o SLH-DSA não
     guarda estado.
   - **Porquê o sal:** cada endereço tem o seu sal, e por isso o hash da folha B é diferente em
     cada endereço. Esse hash fica visível sempre que se gasta pela folha A; sem o sal ligaria os
     endereços entre si.

Há um vetor fixo no teste `p2mr_key_derivation`. O id, a chave Schnorr e o sal foram confirmados
por uma implementação independente em Python. Qualquer alteração à derivação é detetada, porque
faria perder o acesso aos fundos.

## Descritor

```
p2mr(pqseed(<id da semente>)/360h/1h/0h/0h/*h)     forma pública (listdescriptors)
p2mr(pqprv(<semente>)/360h/1h/0h/0h/*h)            forma privada (listdescriptors true, cópia de segurança)
```

- Todos os passos são endurecidos (`h`), e o `*h` só pode ser o último.
- As chaves pós-quânticas de cada endereço ficam na base de dados da carteira
  (`walletdescriptorp2mrcache`, ~1,4 KB por endereço). Assim, a carteira reconhece os endereços
  depois de reiniciar, mesmo bloqueada.
- **Cópia de segurança:** os dois descritores privados, ou `backupwallet`. Importados com
  `importdescriptors` numa carteira vazia, dão os mesmos endereços e, depois da reanálise, o mesmo
  saldo.
- **Sem xpub de observação.** Uma carteira só de observação (loja, exchange) importa endereços
  concretos com `addr(pqc1p…)`.
- **O que fica no ficheiro da carteira:**
  - `walletdescriptorpqseed`: a semente, ou `walletdescriptorcpqseed` com a semente cifrada
    (AES-256, com o id como vetor de inicialização);
  - os descritores, que só contêm o id;
  - o cache das chaves públicas pós-quânticas.

  Não há registos `walletdescriptorkey`, `walletdescriptorckey` nem `walletdescriptorcache`
  (xpub). O script de validação confirma isto.

## Regras da carteira nas redes PQCH

- Uma carteira nova recebe só dois descritores `p2mr()`, um para receber e outro para o troco.
- `getnewaddress` usa `bech32m` (P2MR) por omissão. `-addresstype` e `-changetype` com outros
  valores são recusados.
- `importdescriptors` aceita só `p2mr()`, `addr()` e `raw()`, e estes dois últimos apenas com saídas
  válidas na PQCH. Um `tr()` é recusado: na PQCH a sua saída seria lida como uma raiz P2MR e o
  dinheiro ficaria perdido.
- O troco é sempre P2MR, mesmo que se peça outro tipo (`change_type`, `-changetype`).
- `createwalletdescriptor` é recusado: os descritores `p2mr()` de uma carteira nascem da sua semente.
- Envios para endereços que a rede não aceita (`1…`, `3…`, `pqc1q…`) são recusados antes de criar a
  transação. Todas as saídas voltam a ser verificadas antes de assinar.
- Os assinadores externos (carteiras de hardware atuais) são recusados, porque ainda não assinam P2MR.
- Ao carregar uma carteira, recusa-se a que tenha um descritor ativo que não seja `p2mr()`.
- No modo de teste herdado (`-testactivationheight=pqrules@…`), a carteira volta a comportar-se
  como a do Bitcoin, para os testes herdados.

## Assinatura

- **Folha A:** Schnorr com `key_version` 0 e ML-DSA-44 com `key_version` 1, ambos sobre o hash de
  assinatura do BIP341 com o hash da folha. O contexto FIPS é `PQCH/tx/v1`. As assinaturas são
  "hedged": levam aleatoriedade nova, como recomenda o FIPS 204.
- **Folha B:** SLH-DSA-128s com `key_version` 2. Por agora só se escolhe com a opção de assinatura
  `p2mr_recovery`, nos testes.
- A estimativa de taxas usa os tamanhos exatos do testemunho da folha A.

## Validação

- `src/test/pqch_p2mr_wallet_tests.cpp` cobre:
  - a derivação (determinismo, vetor fixo confirmado em Python);
  - o descritor (formas pública e privada, caminhos endurecidos, expansão com a semente e a partir
    do cache, nenhuma chave de curva elíptica na expansão);
  - a assinatura e verificação das duas folhas, assinaturas alteradas e chaves erradas;
  - os tamanhos para a estimativa de taxas.
- `contrib/pqch/validar_carteira.sh` corre em regtest com 2 nós e faz 39 verificações:
  - cria a carteira e confirma que os tipos antigos são recusados;
  - confirma que o ficheiro da carteira não tem chaves de curva elíptica nem tpub, e que a semente
    fica cifrada numa carteira cifrada;
  - envia entre os nós pela rede P2P;
  - reinicia um nó e volta a gastar;
  - recupera a carteira a partir dos descritores privados;
  - testa a carteira cifrada: bloqueada não assina, e depois de reiniciar e desbloquear assina; uma
    frase errada é recusada.
- As 18 suítes de testes da carteira herdadas do Bitcoin passam.

## Revisão independente

Uma revisão feita por quem não escreveu o código encontrou sete erros, todos corrigidos e testados:

| Erro | Correção |
| --- | --- |
| Assinadores externos podiam dar endereços `tr()` (perdidos na PQCH) | recusados nas redes PQCH, na criação e no carregamento |
| Envios para endereços antigos criavam transações inválidas e prendiam as moedas | destino verificado; todas as saídas verificadas antes de assinar |
| `createwalletdescriptor` e `change_type` ainda criavam tipos antigos | só bech32m |
| A verificação depois de assinar podia aceitar Taproot | usa as regras da rede (`StandardSigningVerifyFlags`) e nunca assina Taproot na PQCH |
| Dois descritores com a mesma chave filha num provedor partilhado | já não se aplica: as chaves vêm da semente e o cache é por índice |
| Chave secreta PQ não era apagada em todos os caminhos | `pqc::KeyPair` apaga-a ao ser destruído |
| O índice `0x7FFFFFFF` podia ser usado como endereço | recusado (é o da chave de recuperação) |

A estimativa de taxas também foi corrigida: contava a contagem de elementos duas vezes e não
contava o byte de sighash. Agora assume sempre o caso maior.

## Segunda revisão independente (semente pós-quântica)

Esta revisão não encontrou erros graves. A semente nunca fica em claro no disco numa carteira
cifrada, nenhum RPC indevido a mostra e não fica guardada nenhuma chave de curva elíptica que leve
até ela. Foram corrigidos:

- `p2mr(S/…/5h)`, de índice fixo, dava um endereço diferente do índice 5 de `p2mr(S/…/*h)`. Passou
  a dar o mesmo.
- `getdescriptorinfo` dizia `hasprivatekeys: false` para `pqprv(…)`.
- Cópias temporárias da semente não eram apagadas.
- A migração de uma carteira antiga do Bitcoin derivava a semente de uma chave cuja chave pública
  ficava no ficheiro antigo. Agora usa uma semente nova.

## Ficheiro da carteira e computador quântico

A primeira versão derivava as chaves de uma cadeia BIP32. O ficheiro da carteira guardava então
chaves públicas de curva elíptica (xpub mestra e xpub filhas). Quem tivesse o ficheiro e um
computador quântico chegava a todas as chaves, mesmo com a carteira cifrada. Por decisão do
projeto (5 nov. 2026), a carteira passou a usar uma semente pós-quântica própria.

Hoje o ficheiro só tem a semente (cifrada, se a carteira estiver cifrada), o id dela e chaves
públicas pós-quânticas. A cifra da carteira (AES-256) continua segura contra computadores
quânticos, mas depende de uma frase-passe forte.

## Pendente

- Escolher a folha de recuperação por RPC (`send` / `walletcreatefundedpsbt`), para o dia em que o
  ML-DSA for desativado.
- PSBT com assinaturas pós-quânticas parciais (multiassinatura, carteiras de hardware). Hoje a
  carteira assina cada entrada P2MR de uma vez só.
- Registar o prefixo no SLIP-0173. O segundo passo do caminho usa `0h` na rede principal.
- Mostrar a árvore P2MR em `getaddressinfo`; hoje o `desc` de um endereço é `addr(…)`.
- Ao desbloquear a carteira, voltar a derivar as chaves P2MR guardadas em cache e compará-las.
- A troca para as regras da carteira usa `PQRulesHeight == 0`, sem olhar para a altura (só afeta
  testes com `-testactivationheight=pqrules@N`).
- Rever os alvos de fuzzing que assinam ou inferem descritores, porque agora leem os parâmetros da
  rede. O `psbt` já foi corrigido.
- A migração de carteiras antigas do Bitcoin para descritores não é suportada nas redes PQCH.
- **Carteira cifrada e bloqueada:** não consegue gerar endereços novos, porque sem a semente nada
  se deriva. Uma reanálise longa com a carteira bloqueada para no limite de endereços já gerados.
  As moedas mais à frente só aparecem depois de desbloquear e reanalisar; não se perdem.
- **`importdescriptors` sem transação na base de dados:** se a importação falhar a meio, pode ficar
  um registo de semente em claro sem descritor. Correr a importação numa transação.
- **Mensagens de erro do `importdescriptors`:** repetem o descritor e, com ele, a semente. O
  Bitcoin faz o mesmo com a xprv.
- **Carteiras de teste da primeira versão** (`p2mr(xprv…)`) já não carregam.
- Carteiras de hardware com ML-DSA e SLH-DSA. Não são compatíveis com as carteiras BIP32 atuais.
- Palavras de recuperação BIP39 (24 palavras, secção 21.5) e a cópia de segurança obrigatória no
  primeiro arranque (secção 20). Ficam para a aplicação. A semente pós-quântica pode sair das 24
  palavras (por exemplo, com um HMAC da semente BIP39), mas isso tem de ficar decidido antes do
  lançamento.
