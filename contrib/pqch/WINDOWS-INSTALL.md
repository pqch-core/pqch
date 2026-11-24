# Installing PQCH Core on Windows

*Português: [INSTALAR-WINDOWS.md](INSTALAR-WINDOWS.md)*

PQCH Core is the full program: network node, wallet and CPU mining. This guide explains how to download
it, check that it is genuine, and install it on Windows 10 or 11 (64-bit).

> **Important.** The Windows installer is **not code-signed yet**. So Windows may show "Unknown
> publisher" or, on Windows 11 with **Smart App Control** on, block the program. This does not mean the
> file is damaged or contains a virus: it only means it does not carry that signature.
>
> **What proves the files are ours is the SHA256 sum signed with GPG.** Always check it before
> installing (steps 2 and 3). A code-signed build will follow, with the same code.

## Requirements

- Windows 10 or 11, 64-bit.
- 4 GB of RAM (tested on a Celeron N4020 with 4 GB).
- About 2 GB of free disk space to start; the chain grows over time.
- An Internet connection.

## 1. Download

Only from **https://github.com/pqch-core/pqch/releases** (or the downloads section of pqch.org, which
points there). Save these to the same folder, for example `Downloads`:

| File | What it is for |
|---|---|
| `pqch-<version>-win64-setup.exe` | the installer |
| `SHA256SUMS` | the SHA256 sums of every file in the release |
| `SHA256SUMS.asc` | the GPG signature of `SHA256SUMS` |
| `pqch-releases.asc` | the release public key |

## 2. Check the GPG signature

You need [Gpg4win](https://gpg4win.org). After installing it, open **PowerShell** in the downloads
folder (in File Explorer: right-click the folder → *Open in Terminal*):

```powershell
gpg --import pqch-releases.asc
gpg --fingerprint "PQCH Releases"
```

The fingerprint must be **exactly**:

```
1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA
```

Check it on pqch.org too. If it is different, **stop and do not install**. Then:

```powershell
gpg --verify SHA256SUMS.asc SHA256SUMS
```

It must say **Good signature from "PQCH Releases"**. The warning "This key is not certified with a
trusted signature" is normal: it only means you have not marked the key as trusted in your GPG. If it
says "BAD signature", **do not install**.

## 3. Check the installer's SHA256 sum

In the same PowerShell:

```powershell
Get-FileHash .\pqch-<version>-win64-setup.exe -Algorithm SHA256
```

Compare the value with the installer's line in `SHA256SUMS` (open it in Notepad). Every character must
match. If it does not, delete the file and download it again.

Without PowerShell: `certutil -hashfile pqch-<version>-win64-setup.exe SHA256` in Command Prompt.

## 4. Install

1. Double-click the installer.
2. If **"Windows protected your PC"** appears (SmartScreen): click **More info**, then **Run anyway**.
3. The permission prompt (UAC) shows **"Publisher: Unknown"**. This is expected: click **Yes**.
4. Follow the installer. At the end you have these shortcuts:
   - **PQCH** — the wallet and node;
   - **PQCH Miner** — opens straight on the Mining tab.

## 5. If Smart App Control blocks the program

On Windows 11 with Smart App Control on, opening PQCH shows:

> "Smart App Control blocked an app that may be unsafe.
> C:\Program Files\PQCH\pqch-qt.exe was blocked because we couldn't confirm who published it…"

Smart App Control **has no per-app exceptions**: either it is on and blocks every program without a
code signature, or it is off.

**Only do this after steps 2 and 3 succeeded.**

1. Click **OK** to close the message. Do not click "Send feedback" (that sends a copy of the program to
   Microsoft).
2. Open **Windows Security** (search for it in the Start menu).
3. Go to **App & browser control**.
4. Click **Smart App Control settings**.
5. Choose **Off** and confirm.
6. Open PQCH again from its shortcut.

**Turning it back on.** On an up-to-date Windows 11 (the April 2026 update or later) you can choose
**On** or **Evaluation** again in the same place. On older Windows builds, once turned off it only comes
back by resetting or reinstalling Windows. If you would rather not turn it off, wait for the code-signed
build.

Turning Smart App Control off does not turn off the antivirus (Microsoft Defender), which stays on.

## 6. First start

1. Choose the data folder. The default is `%LOCALAPPDATA%\PQCH`.
2. Wait for it to sync with the network (on testnet, about 25 minutes on a modest laptop).
3. Create the wallet and **encrypt it with a password**.
4. **Back up** the wallet (*File → Backup Wallet…*) to a USB stick or external drive, and keep the
   password somewhere safe. Never send your password, recovery words or wallet file to anyone.

## 7. Sending

If the network has no fee estimate yet, the wallet uses the fallback fee of 0.0001 PQCH/kB (since
version 1.0.0). In the *Send* tab, the **"Subtract fee from amount"** box:

- **unticked** — the recipient gets the exact amount and the fee comes out of your balance;
- **ticked** — the recipient gets the amount minus the fee.

## 8. Uninstalling

1. Close PQCH (*File → Exit*).
2. **Settings → Apps → PQCH → Uninstall.**
3. Uninstalling **does not delete** the data folder `%LOCALAPPDATA%\PQCH`, which holds your wallet.
   Only delete it once you have a backup of the wallet, or if it is a test install with nothing of value.

## Help

Problems and questions: https://github.com/pqch-core/pqch/issues. No one from the team will ever ask
for your password, recovery words or wallet file.
