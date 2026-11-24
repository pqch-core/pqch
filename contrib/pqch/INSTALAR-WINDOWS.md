# Instalar o PQCH Core no Windows

*English: [WINDOWS-INSTALL.md](WINDOWS-INSTALL.md)*

O PQCH Core é o programa completo: nó da rede, carteira e mineração em CPU. Este guia explica como o
descarregar, confirmar que é autêntico e instalar no Windows 10 ou 11 (64 bits).

> **Importante.** O instalador para Windows **ainda não tem certificado de assinatura de código**.
> Por isso o Windows pode mostrar «Editor desconhecido» ou, no Windows 11 com o **Controlo Inteligente
> de Aplicações** ligado, bloquear o programa. Isto não quer dizer que o ficheiro esteja danificado ou
> tenha vírus: quer dizer apenas que não tem essa assinatura.
>
> **O que prova que os ficheiros são nossos é a soma SHA256 assinada com GPG.** Confirma-a sempre
> antes de instalar (passos 2 e 3). Uma versão com assinatura de código virá a seguir, com o mesmo
> código.

## Requisitos

- Windows 10 ou 11, 64 bits.
- 4 GB de RAM (testado num Celeron N4020 com 4 GB).
- Cerca de 2 GB livres em disco no início; a cadeia cresce com o tempo.
- Ligação à Internet.

## 1. Descarregar

Só de **https://github.com/pqch-core/pqch/releases** (ou da secção de transferências de pqch.org, que
aponta para lá). Descarrega para a mesma pasta, por exemplo `Transferências`:

| Ficheiro | Para que serve |
|---|---|
| `pqch-<versão>-win64-setup.exe` | o instalador |
| `SHA256SUMS` | as somas SHA256 de todos os ficheiros da versão |
| `SHA256SUMS.asc` | a assinatura GPG do `SHA256SUMS` |
| `pqch-releases.asc` | a chave pública das versões |

## 2. Confirmar a assinatura GPG

Precisas do [Gpg4win](https://gpg4win.org). Depois de o instalar, abre o **PowerShell** na pasta das
transferências (no Explorador: botão direito na pasta → *Abrir no Terminal*):

```powershell
gpg --import pqch-releases.asc
gpg --fingerprint "PQCH Releases"
```

A impressão digital tem de ser **exatamente**:

```
1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA
```

Confirma-a também em pqch.org. Se for diferente, **para e não instales**. Depois:

```powershell
gpg --verify SHA256SUMS.asc SHA256SUMS
```

Tem de aparecer **«Good signature from "PQCH Releases"»**. O aviso «This key is not certified with a
trusted signature» é normal: quer dizer apenas que não marcaste a chave como de confiança no teu GPG.
Se aparecer «BAD signature», **não instales**.

## 3. Confirmar a soma SHA256 do instalador

No mesmo PowerShell:

```powershell
Get-FileHash .\pqch-<versão>-win64-setup.exe -Algorithm SHA256
```

Compara o valor com a linha do instalador no `SHA256SUMS` (abre-o no Bloco de Notas). Têm de ser
iguais em todos os caracteres. Se forem diferentes, apaga o ficheiro e descarrega outra vez.

Sem PowerShell: `certutil -hashfile pqch-<versão>-win64-setup.exe SHA256` na Linha de Comandos.

## 4. Instalar

1. Faz duplo clique no instalador.
2. Se aparecer **«O Windows protegeu o seu PC»** (SmartScreen): carrega em **Mais informações** e depois
   em **Executar mesmo assim**.
3. No pedido de permissão (UAC) aparece **«Editor: Desconhecido»**. É esperado: carrega em **Sim**.
4. Segue o instalador. No fim ficam os atalhos:
   - **PQCH** — a carteira e o nó;
   - **PQCH Miner** — abre diretamente no separador de mineração.

## 5. Se o Controlo Inteligente de Aplicações bloquear o programa

No Windows 11, se o Controlo Inteligente de Aplicações estiver ligado, ao abrir o PQCH aparece:

> «O Controlo Inteligente de Aplicações bloqueou uma aplicação que pode não ser segura.
> C:\Program Files\PQCH\pqch-qt.exe foi bloqueado porque não conseguimos confirmar quem o escreveu…»

Esta proteção **não permite exceções por aplicação**: ou está ligada e bloqueia todos os programas sem
assinatura de código, ou está desligada.

**Faz isto só depois de os passos 2 e 3 terem dado certo.**

1. Carrega em **OK** para fechar o aviso. Não carregues em «Enviar comentários» (isso envia uma cópia
   do programa à Microsoft).
2. Abre **Segurança do Windows** (procura «Segurança do Windows» no menu Iniciar).
3. Vai a **Controlo de aplicações e browser**.
4. Carrega em **Definições do Controlo Inteligente de Aplicações**.
5. Escolhe **Desativado** e confirma.
6. Abre o PQCH outra vez pelo atalho.

**Voltar a ligar.** Com o Windows 11 atualizado (atualização de abril de 2026 ou posterior), podes
voltar a escolher **Ativado** ou **Avaliação** no mesmo sítio. Em versões mais antigas do Windows, depois
de desligada, esta proteção só volta a ligar reinstalando ou repondo o Windows. Se preferires não a
desligar, espera pela versão com assinatura de código.

Desligar esta proteção não desliga o antivírus (Microsoft Defender), que continua ativo.

## 6. Primeiro arranque

Os nomes dos menus do PQCH aparecem aqui em inglês, como no programa.

1. Escolhe a pasta de dados. A predefinida é `%LOCALAPPDATA%\PQCH`.
2. Espera pela sincronização com a rede (na testnet, cerca de 25 minutos num portátil modesto).
3. Cria a carteira e **cifra-a com uma palavra-passe**.
4. Faz uma **cópia de segurança** da carteira (menu *File → Backup Wallet…*) para uma pen
   ou disco externo, e guarda a palavra-passe num local seguro.
   Nunca envies a palavra-passe, as palavras de recuperação ou o ficheiro da carteira a ninguém.

## 7. Enviar

Se ainda não houver estimativa de taxa na rede, a carteira usa a taxa de reserva de 0,0001 PQCH/kB
(desde a versão 1.0.0). No separador *Send*, na caixa **«Subtract fee from amount»**:

- **sem visto** — o destino recebe o valor exato e a taxa sai da tua conta;
- **com visto** — o destino recebe o valor menos a taxa.

## 8. Desinstalar

1. Fecha o PQCH (menu *File → Exit*).
2. **Definições → Aplicações → PQCH → Desinstalar.**
3. A desinstalação **não apaga** a pasta de dados `%LOCALAPPDATA%\PQCH`, onde está a tua carteira.
   Só a apagues depois de teres uma cópia de segurança da carteira, ou se for uma instalação de testes
   sem valor.

## Ajuda

Problemas e dúvidas: https://github.com/pqch-core/pqch/issues. Ninguém da equipa te vai pedir a
palavra-passe, as palavras de recuperação ou o ficheiro da carteira.
