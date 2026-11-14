<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**Changes**
Since v0.2.9 (testnet trial; no consensus change):
- Mining: `generatetoaddress` / `generatetodescriptor` start each search at a random nonce instead of 0
  (regtest keeps 0). Two machines preparing the same block (same tip, same payout address, same
  second) no longer try the same nonces in the same order, where the slower one only repeated the
  faster one's work: on the testnet the MacBook won under half its share of blocks.
- A block found on the last allowed try (`maxtries`) is no longer thrown away.
- Wallet (PQCH-Qt): new **Mining** tab (Alt+5). Mines with this computer's processor to a new address
  of the open wallet: choose threads and fast mode, then Start / Stop. Shows your hash rate, the
  network's, your share and the expected time between your blocks. It waits while the node has no
  peers or is still catching up, so it never mines on an old tip. One wallet can mine at a time.
- Wallet (PQCH-Qt): new look in the PQCH colours: tab bar with a highlighted tab, the balance in a
  gradient card, white cards with soft shadows, gradient Send / Receive buttons, cleaner tables.

Earlier, in v0.2.9:
- `getmininginfo` gains `localhashps`: hashes per second of this node's own miner (generate*),
  over the time it spent searching in the last 10 minutes. Compare it with `networkhashps` to see
  the share of blocks a machine should win.

Earlier, from v0.2.4 to v0.2.8:
- Mining: `generatetoaddress` / `generatetodescriptor` stop searching as soon as another block
  arrives and start again on top of it. Before, a slow miner kept hashing on the old tip for up to
  an hour, so with 1-minute blocks almost all of its work went to orphans (on the testnet a laptop
  with 2 threads won 1 block in 600).
- Agent vaults (`contrib/pqch/COFRE.md`): an agent's coins in a P2MR vault that its owner can take back
  with a post-quantum recovery key, at once or after a timelock of up to 65,535 blocks. New RPCs
  `getvaultkey`, `createvault`, `listvaults`, `recovervault`, and the dialog **Settings → Agent Vaults…**.
- `recovervault` never fails for want of a fee estimate: it falls back to 10 quanta/vB and reports
  where the fee rate came from (`fee_source`).
- `listvaults` shows vault coins not yet confirmed (`unconfirmed`); they are never recovered.
- A wallet can no longer make a vault with its own recovery key, and a recovery never sends the coins
  back into a vault (the default address is a new `p2mr()` address of the owner).
- The dialog shows the wallet's name and hides the owner's import data (it holds the vault's seed)
  behind *Show* / *Copy*.

**Verify before installing**
1. Import the release key (`pqch-releases.asc`) and check its fingerprint:
   `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`
2. `gpg --verify SHA256SUMS.asc SHA256SUMS` → "Good signature from PQCH Releases"
3. Linux: `sha256sum --ignore-missing -c SHA256SUMS` · Windows: `certutil -hashfile pqch-{{VERSION}}-win64-setup.exe SHA256`, then compare with the line in `SHA256SUMS`.

The Windows installer is not code-signed, so SmartScreen shows "Unknown publisher". The GPG signature above is what proves the files are ours.
