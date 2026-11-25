# Guião do lançamento da rede principal — sexta, 27 nov. 2026, 23:00 UTC

Lisboa está em UTC em novembro: 23:00 UTC são 23:00 em Lisboa.

Cada passo diz **quem**, **onde** e **como verificar**. Não se avança para o passo seguinte sem a
verificação do anterior. O ensaio geral de quinta, 26 nov., segue este guião com um génesis de
ensaio numa rede descartável e anota quanto tempo leva cada passo.

**Regra de adiamento.** Se um passo falhar e não houver uma correção segura até **20:00 UTC de
sexta**, não se publica nada. Anuncia-se uma nova data nos canais oficiais e no site. O génesis
ainda não é público, por isso adiar não custa nada.

## Máquinas

| Máquina | SSH | Papel no lançamento |
|---|---|---|
| Âncora (Hostinger) | `ssh root@187.127.89.83` | nó público da rede principal, rede do pool (29340), stratum público (29341); **não mina** |
| VPS pública (Hostinger) | `ssh root@72.61.20.80` | nó público da rede principal; site pqch.org |
| Servidor do explorador (casa) | `ssh pqch_explorer@192.168.1.155` | explorador e API `explorer.pqch.org` |
| Kali (casa) | conforme `TESTNET.md` | verificar os binários antes de os instalar nos servidores |
| Server de casa | `ssh server@192.168.1.164` | minera a testnet até às 23:00; depois **minera a rede principal** pelo pool (`minerar_rede_principal.sh`) |
| Kali `sparta` (casa) | `ssh sparta@192.168.1.242` | minera a testnet até às 23:00; depois **minera a rede principal** pelo pool (`minerar_rede_principal.sh`) |
| Mac | — | **continua a minerar a testnet** (mantém a testnet viva para testes) |
| PC principal | — | **só** descarrega o `SHA256SUMS` e assina-o, com `gpg --homedir D:\gnupg-pqch`; nunca descarrega binários |

## Regras que se mantêm

- A RPC nunca fica exposta: só `127.0.0.1`.
- Cópia de segurança ou snapshot antes de mudar cada servidor.
- A chave GPG e a chave Android só existem nas pens D: e E:.
- Nenhuma semente, palavra-passe ou palavra de recuperação na conversa nem em capturas de ecrã.
- Ninguém mina antes das 23:00, mesmo que o código já o permita a partir das 22:50.

---

## Antes de sexta (feito até quinta às 18:00)

- [x] Sementes da rede principal no código (`f4cc884`).
- [x] Nós da âncora e da VPS preparados e desligados (`instalar_rede_principal.sh preparar`).
- [x] Explorador: comando `novo-genesis`. Pool: `--rede main`.
- [x] Ferramenta do génesis (`contrib/pqch/genesis.py --verificar`).
- [ ] Versão 1.0.0 em `CMakeLists.txt` e notas da release (`contrib/pqch/release-notes.md`).
- [ ] Site: secção de downloads com as ligações da 1.0.0, pronta mas **não publicada**.
- [x] A equipa mina como qualquer pessoa (24 nov.); as recompensas vão para uma morada fora das máquinas de teste (passo 9).
- [ ] Carteira **nova** da rede principal no telemóvel (app Android, rede Mainnet), com as 24 palavras só em
      papel. **Não** pode ser a carteira da testnet usada no teste de recuperação cruzada: essas palavras já
      foram escritas no Kali. Tirar dela uma morada `pqc1p…` para as recompensas.
- [ ] Server e Kali `sparta` preparados para minerar a rede principal, sem arrancar nada
      (`sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh preparar --pool <pqch-pool> --threads N [--fast]`).
      O Mac fica na testnet.
- [ ] Ensaio geral completo e código congelado às 18:00.

---

## Sexta, 27 nov.

### Passo 1 — Manchete (Cesar, até às 10:00)

Escolher uma manchete de jornal **do próprio dia 27 nov.**, no formato
`The Times 27/Nov/2026 <título>`. Só caracteres ASCII: sem acentos nem aspas.

### Passo 2 — Génesis no código (Claude, 10:00–11:00)

```bash
python3 contrib/pqch/genesis.py --verificar       # a ferramenta confere os génesis atuais
python3 contrib/pqch/genesis.py --mensagem "The Times 27/Nov/2026 ..." \
        --hora 2026-11-27T23:00:00Z --aplicar     # bits 0x1e7fffff e 7 PQCH por omissão
```

**Verificar:**
- `git diff` só mostra as três linhas do génesis da rede principal em `src/kernel/chainparams.cpp`;
- o nó compila e arranca na rede principal: `getblockhash 0` igual ao id calculado, `time`
  1795820400, `bits` 1e7fffff, recompensa 7,00000000;
- os testes unitários completos e os testes funcionais da CI passam.

Commit «Main network genesis (27 Nov 2026)» em `main`, e push.

### Passo 3 — Compilar a release (Cesar, 11:00–12:45)

No GitHub: **Actions → PQCH Release → Run workflow**, ramo `main`, com a caixa **release**
marcada. Demora cerca de 80 a 95 minutos.

Em paralelo, **Actions → Android** para a APK 1.0.0.

**Verificar:** os workflows ficam verdes e há um rascunho «v1.0.0» em Releases com os ficheiros
Windows e Linux e o `SHA256SUMS`.

### Passo 4 — Verificar os binários (Cesar, no Kali)

```bash
# descarregar os ficheiros do rascunho para uma pasta nova, depois:
sha256sum -c SHA256SUMS                  # todos "OK"
./pqch-1.0.0/bin/pqchd -version | head -1         # "v1.0.0"
```

Arrancar o `pqchd` 1.0.0 na rede principal numa pasta temporária, só para ler o génesis:
`getblockhash 0` tem de ser igual ao id do passo 2.

### Passo 5 — Assinar (Cesar, no PC principal, com a pen D:)

```powershell
gpg --homedir D:\gnupg-pqch --armor --detach-sign --local-user releases@pqch.org SHA256SUMS
gpg --homedir D:\gnupg-pqch --verify SHA256SUMS.asc SHA256SUMS      # "Good signature"
```

- Confirmar que o `SHA256SUMS` é o **da 1.0.0**: a primeira linha tem `pqch-1.0.0-…`.
- Juntar o `SHA256SUMS.asc` ao rascunho.
- A APK: assinar com a chave da pen E:, como na 0.3.6.

### Passo 6 — Publicar (Cesar, cerca das 20:00)

1. Publicar o rascunho no GitHub, com a etiqueta `v1.0.0`.
2. Publicar o site com os downloads:

   ```
   ssh pqch_explorer@192.168.1.155
   ```

   ```bash
   sudo -u pqchbuild git -C /opt/pqch-src/pqch-explorer pull
   sudo /opt/pqch-src/pqch-explorer/deploy/instalar_servidor.sh site
   ```

3. Mensagem curta nos canais: «binários 1.0.0 publicados; a rede arranca às 23:00 UTC».

### Passo 7 — Nós públicos (Cesar com Claude, 20:00–21:30)

Em **cada** nó público, primeiro a âncora e depois a VPS:

```
ssh root@187.127.89.83          # depois: ssh root@72.61.20.80
```

1. Snapshot no painel da Hostinger.
2. Instalar a 1.0.0 assinada, conferida com `gpg --verify` e `sha256sum -c`, em `/usr/local/bin`,
   pelo mesmo procedimento das versões 0.3.x. O nó da testnet volta a arrancar sozinho, e com ele
   o índice e a torneira.
3. Ligar o nó da rede principal:

   ```bash
   bash /root/instalar_rede_principal.sh arrancar
   ```

**Verificar** em cada nó:
- `chain: main`, `blocks: 0`, `bestblockhash` igual ao id do génesis;
- depois de ligados os dois nós: `getconnectioncount` ≥ 1 em cada um, porque se encontram pelas
  sementes fixas;
- a testnet continua a correr: `pqchd-testnet` ativo e altura a subir.

### Passo 8 — Explorador e pool (21:30–22:30)

**Explorador:**

```
ssh pqch_explorer@192.168.1.155
```

```bash
sudo /opt/pqch-src/pqch-explorer/deploy/instalar_servidor.sh novo-genesis   # pede "APAGAR"; tira um snapshot ZFS antes
for r in pqch pqch-electrs pqch-explorer; do sudo -u pqchbuild git -C /opt/pqch-src/$r pull; done
sudo /opt/pqch-src/pqch-explorer/deploy/instalar_servidor.sh tudo
sudo /opt/pqch-src/pqch-explorer/deploy/instalar_servidor.sh estado
```

**Verificar:** `chain: main`, altura 0, o índice responde e o backend devolve 200. O
`explorer.pqch.org` mostra o bloco 0.

**Pool, na âncora:**

```
ssh root@187.127.89.83
```

```bash
git -C <pasta do pqch-pool> pull
sudo <pasta do pqch-pool>/deploy/instalar_pool_testnet.sh --rede main --user pqch \
     --datadir /var/lib/pqch-main --threads 0 --listen --stratum-publico 29341
```

**Verificar:** o serviço `pqch-pool-main` está ativo e as portas 29340 e 29341 aparecem na ufw.

### Passo 8B — Nós dos mineradores da equipa (22:30, server e Kali `sparta`)

```bash
# server (192.168.1.164) e Kali sparta (192.168.1.242): a 1.0.0 assinada, conferida pelo script
sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh binarios 1.0.0
# os dois:
sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh arrancar-no
```

**Verificar:** `"chain": "main"`, `"blocks": 0` e pelo menos 1 ligação. O server não é a máquina
do explorador: tem o seu próprio nó `pqchd-main`. Ainda ninguém mina.

### Passo 9 — Arranque (23:00)

- **Mineração da equipa** (decidido a 24 nov.): a equipa mina, como qualquer pessoa. Não há
  pré-mineração e a mineração abre para todos à mesma hora.
  - Regra de segurança: as recompensas vão para uma **morada** de uma carteira que não está nas
    máquinas de teste, por exemplo a app Android na rede principal.
  - Os mineradores recebem só essa morada: `--morada pqc1p…` no pool, ou o modo pool da carteira com
    morada.
- Às **23:00:00**, e não antes, os mineradores da equipa arrancam. No server e no Kali `sparta`:
  ```bash
  sudo contrib/pqch/servidor-testes/minerar_rede_principal.sh arrancar --morada pqc1p…
  ```
  O script recusa antes das 23:00 UTC. Para a mineração da testnet dessa máquina (o nó da testnet
  continua), instala o pool da rede principal ligado à âncora (29340) e começa a minerar. No Kali,
  parar também a mineração da carteira PQCH-Qt da testnet (separador Mining → Parar). O Mac continua
  na testnet.
  Voltar atrás: `minerar_rede_principal.sh parar` (volta a minerar a testnet).
- Bloco 1: confirmar que os dois nós públicos e o explorador o veem com o mesmo hash.

**Verificar nos primeiros 30 minutos:**
- os blocos entram;
- a dificuldade sobe, como na simulação de `GENESIS.md`;
- nenhum nó regista `high-hash`, `bad-` ou `checkpoint-mismatch` no `debug.log`.

### Passo 10 — Depois do arranque (23:30)

- Anúncio: «A rede principal está no ar», com o hash do bloco 1 e a ligação ao explorador.
- Vigilância: os primeiros blocos de hora a hora durante 24 h, e o nó Monero e o merged mining
  quando estiverem ligados à rede principal.
- Mudar o pop-up do site: deixa de haver contagem decrescente.

## Se algo correr mal depois das 23:00

- **Erro num nó ou num serviço** (explorador, pool): corrige-se esse serviço; a rede continua.
- **Erro de consenso** (nós a discordar do mesmo bloco): parar a mineração da equipa, recolher o
  `debug.log` dos nós e decidir: uma versão 1.0.1, ou, no pior caso nas primeiras horas, recomeçar
  com um génesis novo. Anuncia-se sempre o que aconteceu, com transparência.

## Depois do lançamento (por fazer)

- **Certificado de assinatura de código Certum Open Source** (shop.certum.eu, «Open Source Code Signing — set», com
  cartão e leitor), em nome do Cesar com o prefixo «Open Source Developer». A chave fica no cartão, fora do PC, como
  as chaves GPG. Depois: assinar `pqch-qt.exe`, os outros executáveis e o instalador no processo de release, e publicar
  uma 1.0.1 para Windows com o mesmo código. Motivo: o Controlo Inteligente de Aplicações do Windows 11 bloqueia o
  `pqch-qt.exe` sem assinatura (teste de 24 nov. 2026). Até lá: nota na página de transferências e
  `INSTALAR-WINDOWS.md` / `WINDOWS-INSTALL.md`.
