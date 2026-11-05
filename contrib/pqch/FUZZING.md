# PQCH — Fuzzing

O fuzzing corre com libFuzzer e com os sanitizers ASan e UBSan (`cmake --preset=libfuzzer`).

```
cmake --preset=libfuzzer -DENABLE_WALLET=OFF -DENABLE_IPC=OFF
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

## Pendente

- Corridas longas (horas) num servidor com mais núcleos, e um corpus guardado no repositório.
- Fuzzing com sanitizers na integração contínua.
- Vetores ACVP completos do NIST para ML-DSA e SLH-DSA.
- Rever o alvo `p2p_headers_presync` quando houver um trabalho mínimo de cadeia.
