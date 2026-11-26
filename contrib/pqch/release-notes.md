<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**PQCH 1.0.0 starts the main network** on 27 November 2026 at 23:00 UTC.

- No premine: the genesis block's 7 PQCH go to an output nobody can spend, and mining opens to everyone at
  the same time. No block dated before the genesis is valid, and a node refuses blocks more than 10 minutes
  in the future, so nobody, the team included, can mine before 22:50 UTC.
- The genesis message is a newspaper headline of 27 November 2026, which proves no block was mined before
  that day. Genesis hash: `6f4060bc47a319c80d9baf14de951f6358a0b70bef3e14e126308441cf390208`.
- Block reward 7 PQCH, a block about every minute, halving every 4,207,000 blocks. Difficulty starts at the
  easiest target and adjusts on every block (ASERT, half-life 4 hours).
- Ports: 29333 (P2P) and 29332 (RPC, only on 127.0.0.1 by default). The node finds the network through
  two seed nodes, 187.127.89.83 and 72.61.20.80; there are no DNS seeds yet.
- Explorer and the API for the Android wallet: https://explorer.pqch.org. Pool: the public pool node
  187.127.89.83, pool network port 29340, stratum port 29341 (PQCH-Qt, Mining tab, "Pool").
- The testnet keeps running with the same chain; 0.3.7 and 1.0.0 nodes follow it alike.

**Changes since 0.3.7**
- Main network: final genesis block (above) and its seed nodes (the inherited Bitcoin seed list is gone).
- Security: after every SLH-DSA key generation and signature, the node wipes 64 KiB of stack, where the
  slhdsa-c library leaves a copy of the secret seed. Signatures are unchanged.
- Wallet: the fallback fee is now 10 quanta/vB (0.0001 PQCH/kvB) instead of disabled, so a wallet can send
  on a new network before it has fee estimates. `-fallbackfee=0` turns it off.
- Relay policy (not consensus): a transaction spending an unconfirmed TRUC (version 3) transaction may now
  be up to 4,000 vB instead of 1,000 vB. One post-quantum input is about 1,064 vB, so with the old limit a
  PQCH wallet could not pay the fee of a zero-fee TRUC transaction by CPFP (needed to close Lightning
  channels with an anchor output, PQIP 5).
- Windows installer: "PQCH Miner" shortcuts for the main network (Start menu and desktop); the testnet
  miner shortcut stays in the Start menu only.
- PQCH-Qt, Mining tab: pool mode fills in the main network's public pool node, 187.127.89.83:29341 (it was
  empty on the main network; the testnet keeps 187.127.89.83:37889).
- PQCH-Qt: the first-run screen says 2026 and expects about 1 GB of disk to start.
- Calibration closed with the values the testnet already used: reorganisations limited to 60 blocks, at
  most 4 new Monero keys in 4,320 blocks, merged-mining `tx_extra` up to 512 bytes.
- Tests: NIST ACVP vectors for SLH-DSA-SHA2-128s; constant-time and stack-residue checks of slhdsa-c
  (contrib/pqch/ct/); fixes for a genesis in the future and for sanitizer builds.

No consensus rule changes on the testnet. The code is open source and can be audited by any person or
institution.

**Verify before installing**
1. Import the release key (`pqch-releases.asc`) and check its fingerprint:
   `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`
2. `gpg --verify SHA256SUMS.asc SHA256SUMS` → "Good signature from PQCH Releases"
3. Linux: `sha256sum --ignore-missing -c SHA256SUMS` · Windows: `certutil -hashfile pqch-{{VERSION}}-win64-setup.exe SHA256`, then compare with the line in `SHA256SUMS`.

**Windows: the installer is not code-signed yet.** The GPG signature above is what proves the files are ours.
- The installer shows "Unknown publisher" (UAC/SmartScreen). Expected: check SHA256SUMS first, then choose "Run anyway".
- On Windows 11 with **Smart App Control** on, Windows blocks `pqch-qt.exe` ("Smart App Control blocked an app that may be unsafe"). There is no per-app exception. Only if the SHA256 and GPG checks passed: Windows Security → App & browser control → Smart App Control settings → Off. On Windows 11 with the April 2026 update or later it can be turned back on there; on older builds it stays off until Windows is reset.
- A code-signed build will follow. Its program code will be the same as this release.
- Step-by-step guide: https://github.com/pqch-core/pqch/blob/main/contrib/pqch/WINDOWS-INSTALL.md (Português: INSTALAR-WINDOWS.md).
