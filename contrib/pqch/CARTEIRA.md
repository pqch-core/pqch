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

1. **Semente:** a semente BIP32 normal da carteira, uma chave aleatória de 256 bits. A cópia de
   segurança faz-se com `backupwallet` ou com os descritores privados (`listdescriptors true`).
2. **Caminho:** `m/360h/{0h|1h}/0h/{0h receber | 1h troco}/*h`. O `1h` é para as redes de teste.
   Todos os passos são endurecidos, porque as chaves pós-quânticas não se podem derivar de uma xpub.
3. **Chave filha `k` de cada endereço:** dela saem, por HMAC-SHA512 com etiquetas diferentes,
   três coisas: a chave Schnorr, a semente ML-DSA e o sal.
4. **A chave pública de `k` nunca aparece na cadeia.** Um atacante quântico que tire a chave
   privada Schnorr da chave x-only revelada no gasto não fica a saber nada de `k`. Por isso também
   não consegue calcular a chave ML-DSA, e a proteção híbrida mantém-se.
5. **Chave de recuperação SLH-DSA:** uma só por descritor, derivada da filha no índice
   `0x7FFFFFFF`. Gerar uma chave SLH-DSA demora ~80 ms, demasiado para 1.000 endereços. Reutilizá-la
   é seguro porque o SLH-DSA não guarda estado. O sal por endereço faz com que o hash da folha B
   seja diferente em cada endereço. Esse hash fica visível sempre que se gasta pela folha A, e sem
   o sal ligaria todos os endereços da carteira.

As etiquetas são `PQCH/P2MR/v1/schnorr`, `…/ml-dsa-44`, `…/salt` e `…/slh-dsa-128s`. Há um vetor
fixo no teste `p2mr_key_derivation`: qualquer alteração à derivação é detetada, porque faria perder
o acesso aos fundos.

## Descritor

```
p2mr([fingerprint/360h/1h/0h]tpub…/0h/*h)
```

- Aceita uma xprv ou xpub com o caminho todo endurecido. Um passo não endurecido é recusado.
- As chaves pós-quânticas de cada endereço ficam guardadas na base de dados da carteira
  (`walletdescriptorp2mrcache`, ~1,4 KB por endereço). Assim, a carteira reconhece os endereços
  depois de reiniciar, mesmo estando bloqueada.
- **Sem xpub de observação.** Para uma carteira só de observação (loja, exchange), importam-se
  endereços concretos com `addr(pqc1p…)`.

## Regras da carteira nas redes PQCH

- Uma carteira nova recebe só dois descritores `p2mr()`, um para receber e outro para o troco.
- `getnewaddress` usa `bech32m` (P2MR) por omissão. `-addresstype` e `-changetype` com outros
  valores são recusados.
- `importdescriptors` aceita só `p2mr()`, `addr()` e `raw()`, e estes dois últimos apenas com saídas
  válidas na PQCH. Um `tr()` é recusado: na PQCH a sua saída seria lida como uma raiz P2MR e o
  dinheiro ficaria perdido.
- O troco é sempre P2MR.
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
  - a derivação (determinismo, separação quântica, vetor fixo);
  - o descritor (caminhos endurecidos, expansão com chaves e a partir do cache);
  - a assinatura e verificação das duas folhas, assinaturas alteradas e chaves erradas;
  - os tamanhos para a estimativa de taxas.
- `contrib/pqch/validar_carteira.sh` corre em regtest com 2 nós e faz 26 verificações:
  - cria a carteira e confirma que os tipos antigos são recusados;
  - envia entre os nós pela rede P2P;
  - reinicia um nó e volta a gastar;
  - recupera a carteira a partir dos descritores privados;
  - testa a carteira cifrada.
- As 18 suítes de testes da carteira herdadas do Bitcoin passam.

## Pendente

- Escolher a folha de recuperação por RPC (`send` / `walletcreatefundedpsbt`), para o dia em que o
  ML-DSA for desativado.
- PSBT com assinaturas pós-quânticas parciais (multiassinatura, carteiras de hardware). Hoje a
  carteira assina cada entrada P2MR de uma vez só.
- Registar `coin_type` no SLIP-0044 e o prefixo no SLIP-0173. O caminho usa `0h` na rede principal
  provisoriamente.
- Mostrar a árvore P2MR em `getaddressinfo`; hoje o `desc` de um endereço é `addr(…)`.
- Carteiras de hardware com ML-DSA e SLH-DSA.
- Palavras de recuperação BIP39 (24 palavras, secção 21.5) e a cópia de segurança obrigatória no
  primeiro arranque (secção 20). Ficam para a aplicação; o nó guarda a semente no ficheiro da
  carteira, como o Bitcoin Core.
