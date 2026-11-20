<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**Changes**
In v0.3.1 (testnet; one soft fork that stays inactive unless miners signal for it):
- Mining: `getblocktemplate` works on every network. Its internal placeholder coinbase paid `OP_TRUE`, which
  the post-quantum output rule (coinbase included) rejects, so it failed on testnet and mainnet. Pools and
  external miners use it; the decentralised pool (pqch-pool) needs this version.
- Consensus (soft fork, inactive by default): ML-DSA emergency deployment `mldsaemergency` (version bit 1,
  90% of a 1440-block period). If ML-DSA-44 were ever broken, miners can activate it to make ML-DSA
  signatures invalid; coins stay spendable through the SLH-DSA recovery leaf of every P2MR output, and the
  wallet switches to it by itself. Nodes do not signal unless started with `-signalmldsaemergency`.
- Fixes found by the new nightly Debug and sanitizer runs: the mempool now applies the emergency rule as
  consensus does once it is active; restoring a wallet from the 24 words takes its locks in the same order
  as the rest of the wallet (a potential deadlock with the auto-lock timer).
- PQCH-Qt: `-minerapp` one-click mining mode (no data directory dialog, a wallet is created or opened on the
  first run, opens on the Mining tab, can start mining by itself). Windows installer and Linux package add a
  "PQCH Miner (testnet)" launcher.
- Security automation: nightly CodeQL, long fuzzing and functional tests under ASan/UBSan; supply check
  against the issuance schedule in the test monitor; GitHub Actions pinned by commit hash.

**Verify before installing**
1. Import the release key (`pqch-releases.asc`) and check its fingerprint:
   `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`
2. `gpg --verify SHA256SUMS.asc SHA256SUMS` → "Good signature from PQCH Releases"
3. Linux: `sha256sum --ignore-missing -c SHA256SUMS` · Windows: `certutil -hashfile pqch-{{VERSION}}-win64-setup.exe SHA256`, then compare with the line in `SHA256SUMS`.

The Windows installer is not code-signed, so SmartScreen shows "Unknown publisher". The GPG signature above is what proves the files are ours.
