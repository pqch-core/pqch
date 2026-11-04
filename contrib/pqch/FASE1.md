# PQCH — Fase 1: fundação

Base: Bitcoin Core v32.1 (licença MIT; avisos de copyright mantidos).

## Alterações

| Área | Ficheiro | Alteração |
| --- | --- | --- |
| Oferta máxima | `src/consensus/amount.h` | `MAX_MONEY` = 58.900.000 PQCH |
| Emissão | `src/validation.cpp` | `GetBlockSubsidy`: 12,5 PQCH iniciais |
| Rede principal | `src/kernel/chainparams.cpp` | Halving a cada 2.356.000 blocos; blocos de 60 s; soft forks ativos desde o início; sem dados da cadeia Bitcoin (trabalho mínimo, assumevalid, assumeutxo, seeds); bytes mágicos `a7cadcbf`; porta P2P 29333; prefixo `pqc` |
| Regtest | `src/kernel/chainparams.cpp` | Blocos de 60 s; bytes mágicos `a2f492d4`; porta P2P 49444; prefixo `pqcrt`; novo génesis com saída `OP_RETURN "PQCH"` |
| Portas RPC | `src/chainparamsbase.cpp` | 29332 (principal), 49443 (regtest) |
| Unidades | `src/policy/feerate.h` | `PQCH` e `quantum` |
| Testes | `src/test/validation_tests.cpp` | Subsídio inicial 12,5 e emissão total 58.899.999,74084 PQCH |
| Validação | `contrib/pqch/validar_fase1.sh` | Curva de emissão completa em regtest |

## Génesis da regtest

- Mensagem: `PQCH regtest 04/Nov/2026 Peer-to-peer electronic cash, quantum-safe`
- Tempo: 1296688602 (o do regtest do Bitcoin, 2011: os testes herdados assumem um génesis antigo), nonce 1, bits `0x207fffff`, recompensa 12,5 PQCH
- Hash: `dc23bf8241b903f340911cf5199292b79b6ca42078d45c1dca9ccfe18678364b`
- Raiz de Merkle: `0d1c1bfd8c16966c2f0c7e4a47a387465dd665a10174eb077c589d7a27ef75d7`

## O que fica para fases seguintes

- **Génesis da rede principal:** é um marcador provisório (o do Bitcoin). O definitivo é criado no dia do lançamento, depois de o RandomX estar implementado (fase 3).
- **Testnet3, testnet4 e signet:** continuam com os parâmetros do Bitcoin; serão substituídas por uma testnet PQCH própria (fase 6).
- **Ajuste de dificuldade:** continua o do Bitcoin (a cada 2016 blocos); o ASERT entra na fase 2.
- **Nomes dos programas** (`bitcoind`, `bitcoin-cli`) e pasta de dados: a renomear quando a base estabilizar.
- **Testes unitários e funcionais herdados** que assumem 50 BTC por bloco, o génesis do Bitcoin ou prefixos `bc`/`bcrt`: a atualizar progressivamente.
- **Portas:** provisórias, a confirmar contra outras redes antes do lançamento.
