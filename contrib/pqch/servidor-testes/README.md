# Máquinas de testes: fuzzing contínuo, vigilância e mineração da testnet

Máquinas do projeto, sem nada de pessoal e sem portas abertas (só ligações de saída: GitHub, nós da
testnet, API e torneira). Cada uma tem uma de duas funções:

| Função | Comando | Para que máquina |
|---|---|---|
| `vigiar` | fuzzing contínuo + vigilância da testnet | a menos potente: o fuzzing corre à prioridade mínima |
| `minerar` | minerar a testnet com o RandomX em modo rápido | a mais potente (mais cache L3 por núcleo) |

Em 17 nov. 2026: o antigo runner (i7-7600U, 4 MB de L3, AVX2) mina; o portátil i5-2435M (3 MB de L3)
vigia e faz o fuzzing com 2 processos de 2 GB.

## 1. Fuzzing contínuo (`pqch-fuzz.service`)

Corre os alvos de fuzzing dia e noite, com o libFuzzer, ASan e UBSan (`FUZZING.md`):

- em cada volta vai buscar o `main` público e recompila se mudou;
- cada alvo do PQCH corre 45 minutos e cada alvo herdado de maior risco 15 minutos, em processos
  paralelos (`-fork`), à prioridade mínima (`Nice=19`) e no máximo com 70 % da memória;
- o corpus fica em `/var/lib/pqch-fuzz/corpus/` e cresce de volta para volta;
- uma entrada que faça o programa falhar fica em `/var/lib/pqch-fuzz/falhas/<alvo>-…` e o fuzzing
  continua.

**Se aparecer uma falha:** não a publiques num issue (pode ser uma falha de segurança). Reproduz com
`FUZZ=<alvo> build_fuzz/bin/fuzz <ficheiro>` e segue o `SECURITY.md`.

## 2. Vigilância da testnet (`pqch-vigia.timer`)

Um nó de testnet próprio, sem carteira e sem portas abertas, entra na rede pelas sementes públicas,
como faria qualquer pessoa. De hora a hora, `vigia.sh` escreve uma linha em
`/var/lib/pqch-vigia/estado.csv`:

| Coluna | O quê |
|---|---|
| `altura`, `altura_api` | altura da cadeia no nó e na API pública |
| `ligacoes` | ligações do nó |
| `nos_conhecidos` | endereços de nós da testnet que a rede anuncia, sem as duas sementes |
| `versoes_ligadas` | versões dos nós ligados, por exemplo `PQCH:0.3.0=3` |
| `sementes_ok` | `11` se as duas sementes aceitam ligações na 39333 |
| `torneira` | código HTTP da página da torneira (200 é o normal) |
| `avisos` | número de problemas encontrados nessa hora |

Não guarda endereços IP, só contagens. Cada problema também vai para o registo do sistema
(`journalctl -t pqch-vigia`).

## 3. Mineração (`pqch-minerar-testnet.service`)

Um nó de testnet sem portas abertas e o minerador: espera que o nó sincronize, cria a carteira
`mineracao` (só moedas de teste) e pede um bloco de cada vez com `generatetoaddress`. Usa o modo
rápido do RandomX (`randomxfastmining=1`, ~2,1 GB) e uma thread por núcleo físico
(`randomxminingthreads`, ~2 MB de cache L3 por thread). `pqch-desempenho.service` liga as páginas
grandes transparentes e o modo `performance` do processador.

## Instalar

Ubuntu 24.04 ou Kali (sem atualizações automáticas no Kali), x86_64, com o repositório clonado:

```bash
sudo contrib/pqch/servidor-testes/instalar.sh vigiar     # ou: minerar
```

O script apaga o utilizador do antigo runner (`pqch-runner`), se existir, instala os pacotes e as
atualizações automáticas de segurança, cria utilizadores sem privilégios (`pqch-fuzz` e `pqch-vigia`,
ou `pqch-minerador`), instala o `pqchd` a partir da versão assinada (confere a assinatura e o
SHA256) e arranca os serviços. Com menos de 12 GB de memória, o fuzzing usa 2 processos de 2 GB.
O nó de vigilância usa a porta RPC 39342, para poder coexistir com outro nó na mesma máquina.

## Ver o estado

```bash
sudo /usr/local/bin/pqch-estado
```

(O caminho completo porque, no Kali, o `sudo` não procura em `/usr/local/bin`.)

Na máquina que vigia: as últimas horas da vigilância, os avisos das últimas 24 horas, o alvo de
fuzzing em curso e quantas entradas com falha há para analisar. Na que mina: altura, ligações,
blocos encontrados nas últimas 24 horas e saldo de teste.
