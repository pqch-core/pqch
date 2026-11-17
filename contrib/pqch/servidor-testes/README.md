# Máquina de testes: fuzzing contínuo e vigilância da testnet

Uma máquina do projeto, sem nada de pessoal, que faz duas coisas em segundo plano. Não abre portas:
só faz ligações de saída (GitHub, nós da testnet, API e torneira).

## 1. Fuzzing contínuo (`pqch-fuzz.service`)

Corre os alvos de fuzzing dia e noite, com o libFuzzer, ASan e UBSan (`FUZZING.md`):

- em cada volta vai buscar o `main` público e recompila se mudou;
- cada alvo do PQCH corre 45 minutos e cada alvo herdado de maior risco 15 minutos, com todos os
  núcleos menos um (`-fork`), a prioridade mínima (`Nice=19`) e no máximo 70 % da memória;
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

## Instalar

Numa máquina Ubuntu 24.04 x86_64, com o repositório clonado:

```bash
sudo contrib/pqch/servidor-testes/instalar.sh
```

O script apaga o utilizador do antigo runner (`pqch-runner`), instala os pacotes e as atualizações
automáticas de segurança, cria os utilizadores sem privilégios `pqch-fuzz` e `pqch-vigia`, instala o
`pqchd` a partir da versão assinada (confere a assinatura e o SHA256) e arranca os serviços.

## Ver o estado

```bash
sudo pqch-estado
```

Mostra as últimas horas da vigilância, os avisos das últimas 24 horas, o alvo de fuzzing em curso e
quantas entradas com falha há para analisar.
