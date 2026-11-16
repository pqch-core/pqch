# Runner próprio do GitHub Actions

A CI do PQCH (`.github/workflows/pqch.yml`) pode correr numa máquina do projeto em vez dos runners do
GitHub. Com o plano gratuito, um repositório privado tem uma quota mensal de computação pequena, e os
builds com sanitizers e os testes com RandomX gastam-na em poucos dias. Num runner próprio não há
quota: o custo é a eletricidade da máquina ou o aluguer do servidor.

## A máquina

| | Mínimo | Recomendado |
|---|---|---|
| Sistema | Ubuntu 24.04 LTS, x86_64 | Ubuntu 24.04 LTS, x86_64 |
| CPU | 4 núcleos | 8 núcleos ou mais |
| RAM | 8 GB | 16 GB |
| Disco livre | 60 GB | 100 GB SSD |
| Rede | só ligações de saída (HTTPS para o GitHub) | |

Não é preciso abrir portas: o runner liga-se ao GitHub e vai buscar os jobs.

Pode ser um PC em casa ou um servidor alugado. Um servidor dedicado (ou um VPS com núcleos dedicados)
é mais previsível do que um VPS partilhado, porque o RandomX e o build usam todos os núcleos.

## Instalação

1. No GitHub: **pqch-core/pqch › Settings › Actions › Runners › New self-hosted runner**. Escolhe Linux
   e x64 e copia só o token da linha `./config.sh --url ... --token XXXX`. Vale uma hora.
2. Na máquina, com o repositório clonado:

   ```bash
   sudo contrib/pqch/instalar_runner.sh <TOKEN>
   ```

   O script instala os pacotes, cria o utilizador `pqch-runner` (sem sudo), descarrega o runner e confere
   o SHA256 publicado pelo GitHub, regista-o com as etiquetas `self-hosted, linux, x64, pqch` e
   instala-o como serviço systemd.
3. No GitHub: **Settings › Secrets and variables › Actions › Variables › New repository variable**
   - Nome: `PQCH_RUNS_ON`
   - Valor: `["self-hosted","linux","x64","pqch"]`
4. Confirma em **Settings › Actions › Runners** que o runner aparece como *Idle* (verde). Depois, em
   **Actions › PQCH CI › Run workflow**, lança uma execução manual em `main`.

Para voltar aos runners do GitHub basta apagar a variável `PQCH_RUNS_ON`; o workflow não muda.

## Segurança

- **Pull requests de fora nunca correm neste runner.** No `pqch.yml`, um pull request vindo de um
  fork corre sempre nos runners do GitHub (gratuitos em repositórios públicos), mesmo com
  `PQCH_RUNS_ON` definido. Os outros workflows (`android.yml`, `release.yml`, `ci.yml`) não correm em
  pull requests. Os pull requests de ramos do próprio repositório (só quem tem acesso de escrita)
  continuam a usar o runner.
- **Ao tornar o repositório público** (decidido: na abertura da testnet), no GitHub, por esta ordem:
  1. *Settings › Actions › General › Approval for running fork pull request workflows from
     contributors*: **Require approval for all external contributors**. Um PR de fora só corre
     depois de alguém da equipa ler o código e aprovar.
  2. *Organização pqch-core › Settings › Actions › Runner groups › Default*: o grupo tem de permitir
     **só o repositório `pqch`** (*Selected repositories*), e **Allow public repositories** tem de
     estar ligado, senão o runner deixa de aceitar jobs do repositório público.
  3. *Settings › Code security › Private vulnerability reporting*: **Enable** (`SECURITY.md`).
  4. Confirmar com um PR de teste a partir de um fork que o job corre em `ubuntu-24.04` e não no
     runner próprio.
- **Utilizador sem privilégios.** O serviço corre como `pqch-runner`, sem sudo. Os pacotes de
  compilação são instalados uma vez pelo script; os jobs nunca pedem sudo.
- **Máquina dedicada, ou pelo menos sem segredos.** Não guardes na mesma máquina carteiras, chaves SSH
  de outros projetos ou ficheiros pessoais: um PR malicioso de um membro comprometido teria acesso ao
  que o utilizador `pqch-runner` consegue ler.
- **Atualizações.** O runner atualiza-se sozinho. O sistema operativo não: liga as atualizações
  automáticas de segurança (`sudo apt install unattended-upgrades`).

## Manutenção

| Tarefa | Comando (na pasta `/home/pqch-runner/actions-runner`) |
|---|---|
| Estado do serviço | `sudo ./svc.sh status` |
| Parar / arrancar | `sudo ./svc.sh stop` / `sudo ./svc.sh start` |
| Registos do runner | `journalctl -u 'actions.runner.*' -f` |
| Limpar o ccache | `sudo -u pqch-runner ccache -C` |
| Remover o runner | `sudo ./svc.sh uninstall`, depois `sudo -u pqch-runner ./config.sh remove --token <TOKEN>` (token de remoção na mesma página do GitHub) |

Um runner só faz um job de cada vez; os jobs de um push esperam em fila. Para correr vários em
paralelo, regista mais runners (noutra máquina, ou na mesma com outro nome e outra pasta).
