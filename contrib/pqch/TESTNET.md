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

O agente de utilizador na rede é `/PQCH:<versão>/` (Bitcoin Core: `/Satoshi:<versão>/`). Desde 8 nov. 2026
o PQCH tem numeração própria: 0.x durante a testnet (a primeira é a 0.1.0) e 1.0.0 no lançamento da
rede principal. A base continua a ser o Bitcoin Core 32.1.

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

### Minerar num membro (serviço)

Um membro que mina na testnet corre dois serviços: o nó (`pqchd-testnet`) e o minerador
(`pqch-minerar-testnet`, um ciclo de `generatetoaddress 1 <endereço>`). Para o minerador nunca ficar
parado sem se dar por isso depois de reiniciar o nó (aconteceu a 9 nov. 2026), os dois ficam ligados:

```
sudo mkdir -p /etc/systemd/system/pqchd-testnet.service.d /etc/systemd/system/pqch-minerar-testnet.service.d
printf '[Unit]\nWants=pqch-minerar-testnet.service\n' | sudo tee /etc/systemd/system/pqchd-testnet.service.d/minerador.conf
printf '[Unit]\nPartOf=pqchd-testnet.service\nAfter=pqchd-testnet.service\n' | sudo tee /etc/systemd/system/pqch-minerar-testnet.service.d/seguir-no.conf
sudo systemctl daemon-reload
```

O nó arranca o minerador; parar ou reiniciar o nó para e reinicia o minerador. Para um teste com o
minerador parado (merged mining, por exemplo), pará-lo depois de arrancar o nó:
`sudo systemctl stop pqch-minerar-testnet`.

### Nós públicos da testnet (preparados a 16 nov. 2026)

Dois nós vão servir de entrada à testnet aberta. São também as sementes fixas do código
(`contrib/seeds/nodes_test.txt` → `src/chainparamsseeds.h`, a partir da 0.3.0):

| Nó | IP público | Na VPN | Notas |
|---|---|---|---|
| Âncora | 187.127.89.83 | 10.77.0.1 | centro da VPN e da rede do pool (porta 37890, stratum público 37889); **não mina** (VPS com CPU por créditos: acima de 60% esgota-os em ~3 h e fica limitada a 30%) |
| VPS pública | 72.61.20.80 | 10.77.0.20 | partilha a máquina com um site (nginx); não mina, sem carteira |

Máquinas de casa (IP reservados no router a 17 nov. 2026; ver `servidor-testes/README.md`):

| Máquina | SSH (rede de casa) | Na VPN | Função |
|---|---|---|---|
| Portátil i5-2435M (`cesar@NOSdrive`, Kali) | `ssh -i $HOME\.ssh\id_pqch cesar@192.168.1.233` | 10.77.0.11 | vigilância da testnet e fuzzing contínuo (`sudo /usr/local/bin/pqch-estado`); o antigo nó minerador (`pqchd-testnet`, `pqch-minerar-testnet`) está parado de propósito |
| Portátil Kali (`sparta`) | — | 10.77.0.10 | nó do pool com 8 threads; carteira PQCH-Qt em modo pool |

**Fase A (agora):** a VPS é um membro da VPN como os outros. O nó escuta só em `10.77.0.20`, não
anuncia o IP e liga-se à âncora. Instalação feita com os binários assinados da release (conferidos
com `gpg --verify` e `sha256sum -c`), sem compilar na máquina:

- utilizador `pqch` sem shell; binários `pqchd` e `pqch-cli` em `/usr/local/bin`;
- `/etc/wireguard/pqch.conf` com `PostUp = wg set %i private-key /etc/wireguard/pqch.key` (a chave
  privada gerada na própria VPS; a âncora só recebeu a pública);
- `pqch.conf` com `bind=10.77.0.20`, `addnode=10.77.0.1`, `disablewallet=1`, RPC só local,
  `dbcache=300`, `maxmempool=100`;
- serviço `pqchd-testnet` com limites para o site ter sempre prioridade: `CPUQuota=100%` (1 dos 2
  núcleos), `MemoryHigh=2G`, `MemoryMax=2560M` (a verificação RandomX pode ter até 4 caches de
  256 MiB), `Nice=10`, `IOWeight=50`; depende de `wg-quick@pqch`;
- firewall `ufw`: entra só SSH (`limit`), 80 e 443 (site) e a 39333 só pela interface `pqch`.

**API da testnet para carteiras leves** (16 nov. 2026; o explorador `explorer.pqch.org` fica só
para a rede real): `https://api-testnet.pqch.org/api/`, na VPS pública, sem páginas.

- `pqch-electrs` (≥ `24d809b`, que aceita as versões 0.x do PQCH) como serviço
  `pqch-electrs-testnet`: utilizador `electrs`, índice em `/var/lib/pqch-electrs`, só em
  `127.0.0.1:3000`, `--jsonrpc-import`; limites `CPUQuota=50%`, `MemoryMax=1G`.
- RPC: utilizador `electrs` por `rpcauth` (palavra-passe só em `/etc/pqch-api/electrs.env`, root),
  com `rpcwhitelist` de leitura e `sendrawtransaction`; `rpcwhitelistdefault=0` deixa o cookie local
  com acesso total.
- nginx: ficheiro próprio `/etc/nginx/sites-available/pqch-api-testnet` (os sites da máquina ficam
  intactos); certificado Let's Encrypt por *webroot* (`/var/www/pqch-acme`, renovação automática com
  `--deploy-hook "systemctl reload nginx"`); `/api/` → índice, 10 pedidos/s por IP (rajada 40),
  só GET e POST, corpo até 2 MB; tudo o resto 404; HTTP redireciona para HTTPS.
- DNS: registo A `api-testnet` → 72.61.20.80, sem AAAA (o nginx da VPS só escuta em IPv4).

**Torneira** (16 nov. 2026): `https://faucet-testnet.pqch.org`, 4 moedas por pedido
(`contrib/pqch/torneira/`). Carteira `torneira` no nó da VPS (sem cifra, só moedas de teste),
utilizador RPC `faucet` limitado a `validateaddress`, `sendtoaddress` e `getbalances`;
`fallbackfee=0.0001` no `pqch.conf` da VPS. Primeiro pedido real pelo browser às 14:43.

**Reabastecer a torneira:** a partir da carteira `mineracao` da âncora, **em parcelas de 250**:
`for i in $(seq 1 16); do c -named sendtoaddress address=<endereço da torneira> amount=250 fee_rate=10 || break; done`.
Uma recompensa de bloco vale 7 PQCH e cada entrada P2MR conta cerca de 1 kvB (a assinatura híbrida tem
~3,9 kB e a testemunha conta um quarto), por isso um envio de milhares de moedas teria centenas de
entradas e passaria o limite de tamanho de uma transação padrão (100 kvB, ~98 entradas, ~680 moedas
de recompensas). 250 moedas são ~36 entradas (~36 kvB).

**Fase B (feita a 17 nov. 2026, testnet aberta):** os dois nós escutam em todas as interfaces na
porta 39333, com `externalip` (o IP público de cada um), `maxconnections=40` e `fixedseeds=1`; a RPC
continua só local e a cópia da configuração anterior ficou em `pqch.conf.fase-a`. Teste de fora:
`Test-NetConnection <IP> -Port 39333` deu `True` nos dois. Passos usados:

1. Snapshot da máquina.
2. `pqch.conf`: trocar `bind=10.77.0.x` por escutar em todas as interfaces (apagar a linha `bind`),
   `externalip=<IP público>`, `maxconnections=40` e `fixedseeds=1`; manter a RPC só local.
3. Firewall: `ufw allow 39333/tcp comment 'PQCH testnet publica'` (e confirmar que o painel da
   Hostinger não bloqueia a porta).
4. Reiniciar o nó e testar de fora: `Test-NetConnection 72.61.20.80 -Port 39333` no PowerShell.
5. Os membros da equipa continuam na VPN sem mudar nada.

### Rede principal nos nós públicos (preparada a 23 nov. 2026)

Lançamento a 27 nov. 2026, 23:00 UTC. A âncora e a VPS têm o nó da rede principal preparado com
`contrib/pqch/instalar_rede_principal.sh preparar <IP> <quota>` (âncora 40%, VPS 100%):

- pasta `/var/lib/pqch-main`, `pqch.conf` com `externalip`, `maxconnections=40`, `fixedseeds=1`,
  `dnsseed=0`, `disablewallet=1` e RPC só em 127.0.0.1;
- serviço `pqchd-main` desligado; recusa arrancar com um `pqchd` anterior ao 1.0.0 (génesis
  provisório). No dia: instalar a 1.0.0 assinada e `instalar_rede_principal.sh arrancar`;
- ufw: 29333/tcp aberta; a 29332 (RPC) fechada. Sem firewall no painel da Hostinger nas duas máquinas;
- VPS: SSH só com chave desde 23 nov. 2026 (`/etc/ssh/sshd_config.d/00-pqch-so-chave.conf`; recuperação pelo Console web da Hostinger);
- pool da rede principal na âncora: `instalar_pool_testnet.sh --rede main` (rede do pool na 29340,
  stratum público na 29341), depois de o nó arrancar;
- explorador (servidor de casa): já na rede principal; no dia, `instalar_servidor.sh novo-genesis`
  (snapshot ZFS e apaga a cadeia provisória), `git pull` e `tudo`.

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
