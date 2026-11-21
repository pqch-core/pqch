<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**Changes**
In v0.3.2 (testnet; no consensus changes from v0.3.1):
- PQCH-Qt, Mining tab: **pool mode**. Choose "Pool (shared rewards)" and a pool node (on testnet the public
  pool node 187.127.89.83:37889 is filled in); the wallet makes a new address and runs the pool miner, and
  shows its hash rate and accepted shares. The pool pays a share of every block it finds, by PPLNS, to every
  miner, instead of whole blocks rarely.
- The pool miner is `pqch-pool` v0.1.0 (the PQCH decentralised pool, derived from P2Pool, GPL-3.0), shipped as
  its own program next to pqch-qt (`pqch-pool.exe` on Windows, `bin/pqch-pool` on Linux) and taken from its
  signed release (https://github.com/pqch-core/pqch-pool/releases/tag/v0.1.0) by version and SHA256.
  `-poolminer=<path>` points the wallet at another copy. Antivirus programs often flag mining software:
  check the files with SHA256SUMS below.

**Verify before installing**
1. Import the release key (`pqch-releases.asc`) and check its fingerprint:
   `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`
2. `gpg --verify SHA256SUMS.asc SHA256SUMS` → "Good signature from PQCH Releases"
3. Linux: `sha256sum --ignore-missing -c SHA256SUMS` · Windows: `certutil -hashfile pqch-{{VERSION}}-win64-setup.exe SHA256`, then compare with the line in `SHA256SUMS`.

The Windows installer is not code-signed, so SmartScreen shows "Unknown publisher". The GPG signature above is what proves the files are ours.
