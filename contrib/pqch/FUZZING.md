# PQCH — Fuzzing

O fuzzing corre com libFuzzer e com os sanitizers ASan e UBSan (`cmake --preset=libfuzzer`).

```
cmake --preset=libfuzzer -DENABLE_WALLET=ON -DBUILD_GUI=OFF -DENABLE_IPC=OFF
cmake --build build_fuzz -j$(nproc)
FUZZ=pqch_auxpow_header build_fuzz/bin/fuzz -max_total_time=600 corpus/pqch_auxpow_header
```

## Alvos novos (`src/test/fuzz/pqch.cpp`)

| Alvo | O que exercita | Propriedades verificadas |
| --- | --- | --- |
| `pqch_auxpow_header` | cabeçalhos vindos da rede, com ou sem prova AuxPoW | sem falhas; a serialização dá ida e volta; as verificações da prova nunca falham com qualquer prova |
| `pqch_auxpow_monero_block` | blocos Monero como o P2Pool os submete | o que `BuildFromMoneroBlock` aceita passa sempre na verificação da coinbase e dá um hashing blob |
| `pqch_monero_primitives` | árvore e provas do Monero, parâmetros de merged mining, varints, Keccak, `tx_extra` | as provas levam à raiz; decodificar é o inverso de codificar; Keccak por partes, ou a partir do estado intermédio, igual ao de uma vez |
| `pqch_pq_verify` | verificação ML-DSA e SLH-DSA com dados arbitrários | sem falhas; uma assinatura válida verifica; mudar um byte da assinatura, da mensagem, do contexto ou da chave faz falhar |
| `pqch_p2mr_descriptor` | descritores `p2mr()` | sem falhas; o texto impresso volta a dar o mesmo descritor; a expansão com a semente é igual à expansão a partir do cache |
| `pqch_bip39` | as 24 palavras de recuperação (BIP39), a partir de texto qualquer e de entropia qualquer | sem falhas; o que é aceite volta à forma canónica; 24 palavras da lista, em minúsculas, separadas por um espaço; a entropia volta igual; qualquer espaço ASCII entre palavras dá o mesmo; uma maiúscula, palavras a mais ou a menos são recusadas; trocar uma palavra é recusado ou dá outra entropia; a frase-passe só é aceite em ASCII imprimível; a mesma frase dá a mesma semente e outra frase dá outra |

## Resultados (5 nov. 2026, máquina de 2 núcleos)

| Alvo | Execuções | Resultado |
| --- | --- | --- |
| `pqch_auxpow_header` | ~2,9 milhões | sem erros |
| `pqch_auxpow_monero_block` | ~3,2 milhões | sem erros |
| `pqch_monero_primitives` | ~1,6 milhões | **erro encontrado e corrigido** (ver abaixo) |
| `pqch_pq_verify` | ~335 mil | sem erros |
| `pqch_p2mr_descriptor` | ~34 mil | sem erros (a primeira falha era do próprio alvo: faltava iniciar o contexto secp256k1) |
| `headers_sync_state` (herdado) | ~1,3 milhões | sem erros |
| `descriptor_parse` (herdado) | ~1,7 milhões | sem erros |
| `script_sign` (herdado) | ~378 mil | sem erros |
| `block_header` (herdado) | ~954 mil | sem erros |
| `psbt` (herdado) | ~1,1 milhões | sem erros |
| `p2p_headers_presync` (herdado) | — | o próprio alvo assume um trabalho mínimo de cadeia maior que zero, e a PQCH ainda não tem um; rever quando a rede principal tiver um valor |

**Erro encontrado:** `MoneroRootFromProof` entrava num ciclo infinito com tamanhos de árvore
acima de 2^63. O cálculo da largura da árvore transbordava. O consenso não era afetado, porque os
limites (2^20 transações, 256 cadeias) são verificados antes. Ainda assim, a função passou a recusar
árvores maiores que 2^32 e a largura usa `std::bit_floor`. Há um teste de regressão em
`pqch_auxpow_tests`.

## Resultados (16 nov. 2026, máquina de 2 núcleos, ASan + UBSan)

**Passagem 1: todos os 228 alvos, 500 execuções cada.** 223 passaram à primeira.

| Alvo | Resultado |
| --- | --- |
| `pow_transition` (herdado) | falha do próprio alvo: criava cadeias com uma dificuldade zero, negativa ou acima de `powLimit` no bloco de referência do ASERT. Na PQCH a referência é sempre o génesis, por isso nenhum par a escolhe. O alvo passou a usar `SetCompact` com as verificações de valor negativo e transbordo e a trocar valores inválidos por `powLimit`. Depois: ~10 mil execuções sem erros |
| `connect_block` (herdado) | falha do próprio alvo: gastava recompensas de 50 (Bitcoin) em vez de 12,5 e ligava ao acaso o bit AuxPoW sem prova Monero. Corrigido (saídas 3,75 + 3,75 + 2,5 + 2,5; bit AuxPoW retirado da versão). Depois: ~200 mil execuções sem erros |
| `utxo_snapshot`, `utxo_snapshot_invalid` (herdados) | não se aplicam: exigem dados *assumeutxo*, que a PQCH não tem |
| `p2p_headers_presync` (herdado) | não se aplica ainda (sem trabalho mínimo de cadeia, ver acima) |

**Passagem 2: 5 minutos por alvo nos alvos de maior risco.** Nenhum erro.

| Alvo | Execuções |
| --- | --- |
| `descriptor_parse` | ~2,9 milhões |
| `mocked_descriptor_parse` | ~2,8 milhões |
| `script_sign` | ~610 mil |
| `psbt` | ~1,9 milhões |
| `script` | ~1,9 milhões |
| `miniscript_string` | ~3,3 milhões |
| `miniscript_smart` | ~275 mil |
| `signature_checker` | ~2,2 milhões |
| `script_flags` | ~2,4 milhões |
| `script_interpreter` | ~2,1 milhões |
| `key` | ~194 mil |
| `pqch_p2mr_descriptor` | ~56 mil |
| `pqch_pq_verify` | ~454 mil |
| `pqch_auxpow_header` | ~2,6 milhões |
| `headers_sync_state` | ~1,6 milhões |
| `block_header` | ~2,2 milhões |
| `pow` | ~582 mil |

Nenhum dos alvos encontrou um erro no código do nó nesta ronda.

## Resultados (17 nov. 2026, máquina de 2 núcleos, ASan + UBSan)

| Alvo | Execuções | Resultado |
| --- | --- | --- |
| `pqch_bip39` (novo) | ~163 mil em 10 minutos | sem erros; corpus de 313 entradas |

**Alvos da carteira (17 nov. 2026).** Até aqui o fuzzing era compilado sem a carteira
(`ENABLE_WALLET=OFF`), por isso os 13 alvos da carteira nunca tinham corrido. Compilados com a
carteira, passaram todos 500 execuções. Nas redes PQCH a carteira só aceita descritores `p2mr()`,
`p2mr_vault()`, `addr()` e `raw()`, e os descritores simulados do fuzzing só tinham chaves de curva
elíptica: o `scriptpubkeyman` quase não passava da recusa. O conversor simulado
(`test/fuzz/util/descriptor.cpp`) passou a ter sementes pós-quânticas: `%qNN` dá `pqprv(SEMENTE)` e
`%pNN` dá `pqseed(ID)`.

| Alvo | Execuções | Cobertura | Resultado |
| --- | --- | --- | --- |
| `scriptpubkeyman` (com sementes PQ) | ~483 mil em 5 min | 2.806 → 8.101 | sem erros |
| `mocked_descriptor_parse` (com sementes PQ) | ~3,2 milhões em 5 min | 3.695 → 10.510 | sem erros |
| `spkm_migration` | ~200 mil em 5 min | | sem erros |
| `coincontrol` | ~2,2 milhões em 5 min | | sem erros |
| `wallet_create_transaction` | ~19 mil em 5 min | | sem erros |
| os outros 8 alvos da carteira | 500 cada | | sem erros |

A partir daqui o CI e a máquina de testes compilam o fuzzing com a carteira.

## Pendente

- Corpus guardado no repositório.

As corridas longas fazem-se agora na máquina de testes, sem parar: `servidor-testes/README.md`.
- Fuzzing com sanitizers na integração contínua.
- Vetores ACVP completos do NIST para ML-DSA e SLH-DSA.
- Rever o alvo `p2p_headers_presync` quando houver um trabalho mínimo de cadeia.
