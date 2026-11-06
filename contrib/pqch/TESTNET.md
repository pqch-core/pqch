# PQCH — Identidade e testnet

## Nomes

| Programa | Bitcoin Core | PQCH |
|---|---|---|
| Nó | `bitcoind` | `pqchd` |
| Linha de comandos | `bitcoin-cli` | `pqch-cli` |
| Ferramenta de carteira | `bitcoin-wallet` | `pqch-wallet` |
| Lançador | `bitcoin` | `pqch` |
| Testes unitários | `test_bitcoin` | `test_pqch` |
| Outros | `bitcoin-tx`, `bitcoin-util`, `bitcoin-qt`, `bench_bitcoin` | `pqch-tx`, `pqch-util`, `pqch-qt`, `bench_pqch` |

Os alvos do CMake mantêm os nomes do Bitcoin Core (por exemplo `cmake --build build --target
test_bitcoin`); só os ficheiros gerados mudam de nome. Assim os ficheiros de compilação herdados e as
futuras fusões com o Bitcoin Core ficam simples.

O agente de utilizador na rede é `/PQCH:<versão>/` (Bitcoin Core: `/Satoshi:<versão>/`). A versão
ainda é a do Bitcoin Core em que o PQCH se baseia (32.1.0); a numeração própria fica para o lançamento.

## Pasta de dados e configuração

| Sistema | Pasta |
|---|---|
| Linux | `~/.pqch` |
| macOS | `~/Library/Application Support/PQCH` |
| Windows | `%LOCALAPPDATA%\PQCH` |

- Ficheiro de configuração: `pqch.conf`. Ficheiro PID: `pqchd.pid`.
- Subpastas: `testnet/` e `regtest/`. A rede principal usa a própria pasta.
- O PQCH nunca usa a pasta do Bitcoin Core (`~/.bitcoin`), onde pode haver carteiras de Bitcoin. Se
  alguém apontar `-datadir` para uma pasta de outra rede, o nó para ao arrancar com "Incorrect or no
  genesis block found", antes de abrir as carteiras.

## Redes

| Rede | Opção | P2P | RPC | Endereços | Bytes mágicos |
|---|---|---|---|---|---|
| Principal | (nenhuma) | 29333 | 29332 | `pqc1…` | `a7 ca dc bf` |
| Testnet | `-testnet` | 39333 | 39332 | `pqct1…` | `e8 f4 ed 9b` |
| Regtest | `-regtest` | 49444 | 49443 | `pqcrt1…` | `a2 f4 92 d4` |

A testnet4 e a signet do Bitcoin não existem no PQCH: `-testnet4`, `-signet` ou `-chain=signet`
dão erro. O código delas fica só para os testes unitários herdados.

### Portas: conferência

Portas por omissão de redes conhecidas (P2P / RPC), para não colidir:

| Rede | Principal | Testnet / stagenet |
|---|---|---|
| Bitcoin | 8333 / 8332 | 18333, 48333, 38333 / 18332, 48332, 38332 |
| Monero | 18080 / 18081 | 28080 / 28081, 38080 / 38081 |
| P2Pool (Monero) | 37889 / 37888 (mini) | — |
| Litecoin | 9333 / 9332 | 19335 / 19332 |
| Dogecoin | 22556 / 22555 | 44556 / 44555 |
| Zcash | 8233 / 8232 | 18233 / 18232 |
| Dash | 9999 / 9998 | 19999 / 19998 |
| Tari | 18189, 18141–18143 | — |
| Ethereum (execução) | 30303 | — |

As portas do PQCH (29332–29333, 39332–39333, 49443–49444) não coincidem com nenhuma destas. A lista
foi feita com o que se sabe das redes; antes do lançamento confere-se outra vez com o registo da IANA e
com as redes que entretanto surgirem.

## Testnet

Mesmas regras de consenso da rede principal (RandomX, ASERT, P2MR, AuxPoW, reorganização máxima de
60 blocos). Diferenças:

- Moedas sem valor.
- Maturação das recompensas: 100 blocos (rede principal: 720), para os testadores poderem gastar no
  próprio dia.
- Bloco génese próprio, com a manchete principal de thetimes.com do dia em que a testnet foi criada:
  `The Times 06/Nov/2026 Vance has an awkward problem and little time to solve it`
  (hash `b3079ef1e4520057cda8cfb841850523d295519c1815a8e3287e2a9c465d9caa`, hora 2026-11-06 00:00 UTC).
  Como no Bitcoin, a manchete prova que a cadeia não existia antes desse dia. O génese não tem prova de
  trabalho RandomX (a chave RandomX do primeiro período é o próprio hash do génese); os nós não a
  verificam no bloco 0.
- Pode ser recomeçada com um génese novo, se for preciso.

### Como as pessoas encontram a testnet

Um nó novo procura outros nós por esta ordem:

1. **`-addnode` / `-connect`:** endereços indicados à mão.
2. **Sementes de DNS:** nomes (por exemplo `seed.testnet.<domínio>`) que devolvem endereços de nós
   ativos. Exigem um domínio e um servidor que vá vendo que nós estão de pé.
3. **Sementes fixas:** lista de endereços escrita no código, usada quando o DNS falha.
4. **Troca de endereços:** depois de ligado a um nó, o nó novo recebe endereços de outros e passa a
   encontrá-los sozinho.

A testnet **não tem sementes de DNS nem sementes fixas**: um nó de fora não tem por onde começar.

## Fase atual: testnet fechada à equipa

Na primeira fase só a equipa conhecida usa a testnet. Uma blockchain não tem contas nem
palavras-passe: qualquer nó que chegue à porta de outro pode ligar-se. Por isso o fecho faz-se na
rede, em camadas:

1. **Sem sementes no código** (acima).
2. **Rede privada (VPN) da equipa:** WireGuard ou Tailscale. Cada nó só escuta no endereço da VPN e
   só se liga aos endereços da VPN. Quem não está na VPN nem chega à porta. Não se abre nenhuma porta
   no router.
3. **Firewall:** nos nós sempre ligados, a porta 39333 só aceita a interface da VPN.
4. **Código e binários** só para a equipa (o repositório é privado).

### Instalação do nó âncora (automática)

Num servidor Ubuntu 24.04 limpo, com o repositório clonado:

```bash
sudo contrib/pqch/instalar_ancora_testnet.sh
```

Atualiza o sistema, cria o utilizador `pqch` (sem sudo), compila e instala o PQCH, monta o centro da
VPN (`10.77.0.1`, UDP 51820), ativa a firewall (só SSH e VPN entram; a 39333 só pela VPN), escreve o
`pqch.conf` e instala o serviço `pqchd-testnet`. Para cada membro da equipa:

```bash
sudo contrib/pqch/adicionar_membro_vpn.sh <nome> <n.º 2-254> [chave pública do membro]
```

mostra a configuração WireGuard e o `pqch.conf` que o membro usa. O resto desta secção explica o que
os scripts fazem, para quem prefira montar à mão.

### Rede privada

Exemplo com WireGuard e a rede `10.77.0.0/24` (um endereço por membro; as chaves geram-se com
`wg genkey | tee privada | wg pubkey > publica` e trocam-se só as públicas):

```
# /etc/wireguard/pqch.conf no nó âncora (10.77.0.1), que tem IP público e a porta UDP 51820 aberta
[Interface]
Address = 10.77.0.1/24
ListenPort = 51820
PrivateKey = <chave privada deste nó>

[Peer]   # um bloco por membro
PublicKey = <chave pública do membro>
AllowedIPs = 10.77.0.2/32
```

```
# /etc/wireguard/pqch.conf num membro (10.77.0.2)
[Interface]
Address = 10.77.0.2/24
PrivateKey = <chave privada do membro>

[Peer]
PublicKey = <chave pública do nó âncora>
Endpoint = <IP público do nó âncora>:51820
AllowedIPs = 10.77.0.0/24
PersistentKeepalive = 25
```

Ligar com `sudo systemctl enable --now wg-quick@pqch`. O Tailscale faz o mesmo sem configurar
chaves à mão (cada membro entra na "tailnet" da equipa com uma conta).

### `pqch.conf` de cada membro

```
testnet=1
[test]
# só a rede privada
bind=10.77.0.2          # o endereço deste membro na VPN
addnode=10.77.0.1       # o nó âncora (porta 39333 por omissão)
addnode=10.77.0.3       # outros membros, se estiverem sempre ligados
dnsseed=0
fixedseeds=0
discover=0              # não anunciar os IPs públicos desta máquina
natpmp=0                # não pedir ao router para abrir portas
listenonion=0
# RPC só local (já é assim por omissão)
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

Na firewall do nó âncora (exemplo com `ufw`):

```
sudo ufw allow 51820/udp                       # WireGuard
sudo ufw allow in on pqch to any port 39333    # testnet, só pela VPN
```

### Cuidados

- O runner da integração contínua corre código de pull requests: não deve ser o nó âncora.
- Não pôr carteiras com valor real nas máquinas da testnet.
- Para abrir a testnet mais tarde: nós públicos, sementes fixas e de DNS numa versão nova. Os nós da
  equipa continuam a funcionar sem mudar nada.

## Fases seguintes

1. **Testnet aberta:** um ou dois nós públicos com IP fixo e a porta 39333 aberta (a RPC nunca),
   sementes fixas no código, instruções `pqchd -testnet`.
2. **Semente de DNS:** quando houver um domínio para o projeto.
3. **Torneira (faucet) e explorador de blocos.**
