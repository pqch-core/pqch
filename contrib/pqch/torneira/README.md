# Torneira da testnet PQCH

Página `https://faucet-testnet.pqch.org` que envia **4 moedas de teste** para um endereço da testnet
(decidido a 15 nov. 2026). Corre na VPS pública (72.61.20.80), ao lado do nó e da API.

## Peças

| Peça | Ficheiro | Na VPS |
|---|---|---|
| Programa (só biblioteca padrão do Python) | `faucet.py` | `/opt/pqch-faucet/faucet.py`, utilizador `faucet`, `127.0.0.1:3010` |
| Serviço | `pqch-faucet-testnet.service` | `/etc/systemd/system/` |
| nginx (HTTPS, 2 pedidos/s por IP) | `nginx-faucet-testnet.conf` | `/etc/nginx/sites-available/pqch-faucet-testnet` |
| Carteira | — | carteira `torneira` no nó da VPS, sem cifra (só moedas de teste) |

## Regras

- 4 moedas por pedido; 1 pedido por endereço e 1 por IP a cada 24 horas.
- Teto de 400 moedas por 24 horas para todos, e no máximo 10 envios por minuto.
- Os IPs só ficam guardados como hash com sal e são apagados ao fim de 48 horas; não vão para os
  registos.
- A verificação e o registo de cada pedido fazem-se sob um único cadeado: dois pedidos ao mesmo
  tempo não passam os dois.

## Permissões no nó

O programa usa um utilizador RPC próprio, `faucet`, que só pode chamar `validateaddress`,
`sendtoaddress` e `getbalances` (`rpcwhitelist`). A palavra-passe fica só em
`/etc/pqch-api/faucet.env` (root). O nó precisa de `fallbackfee` para enviar quando ainda não há
estimativa de taxas.

## Testado

Contra um nó regtest (16 nov. 2026): envio, recusa do mesmo endereço, recusa do mesmo IP, teto
diário, endereço inválido, e um comando fora da lista (`getblockcount`) recusado com HTTP 403.

## Reabastecer

A partir de uma carteira da equipa com moedas de teste maduras:
`pqch-cli -testnet sendtoaddress <endereço da torneira> 4000`. O endereço da torneira obtém-se na
VPS com `sudo -u pqch -H pqch-cli -testnet -rpcwallet=torneira getnewaddress`.
