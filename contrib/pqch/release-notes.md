<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**Changes**
In v0.3.7 (testnet; no change to which chain is valid):
- Merged mining from home: a node now checks a header with a new Monero key from an inbound peer too,
  at most one a minute, with a RandomX cache of its own and outside the main lock. Before, such headers
  were only checked from outbound peers, so a merged miner whose node only connects out (behind a home
  router, the usual case for P2Pool miners) never got its blocks accepted by the network. A header with
  fake work is refused, its sender punished, and no cache in use is pushed out.
- Installer (Windows): no "test signet" or "testnet4" shortcuts (PQCH has neither network).

In v0.3.6 (testnet; no change to which chain is valid):
- Android wallet: if the network ever activates the ML-DSA emergency soft fork, the app switches to
  the SLH-DSA recovery key by itself. A payment the node rejects for that reason is not sent; the app
  signs the same payment again with the recovery key and asks for a new confirmation (the fee is
  higher), and later payments use the recovery key from the start. New APK, signed with the release
  key (certificate SHA-256 39:86:31:73:…:F3:D2:ED).
- Light wallet core: create_tx can sign through the recovery leaf (recovery), with the fee for its size.

In v0.3.5 (testnet; no change to which chain is valid):
- Header checkpoints: every release ships the hash of one block in 500. A header at a checkpoint height
  must have that hash, and the headers that lead to it are not hashed with RandomX (about 25 ms each),
  so a new node syncs the block headers much faster. Everything else in a header is still checked and
  blocks are validated in full. The testnet ships 44 checkpoints (up to block 22,000), read from two
  public nodes that agree; the main network starts without any.
- PQCH-Qt: a main window saved as big as the screen opens at 80% of it, centred, so it can be resized
  (un-maximizing used to leave it filling the screen).
- Tests: the two inherited header sync functional tests fixed (one-minute blocks; batched generation).

In v0.3.4 (testnet; no consensus changes):
- PQCH-Qt, Mining tab: fits narrow windows. The Start button moves to its own full-width row instead of
  going off the right edge, the stat cards reflow to two columns, and the window is never narrower than
  the tab bar.
- PQCH-Qt: scroll bars in the logo violet, so it is obvious when a page scrolls.
- PQCH-Qt, pool mode: "Your share" no longer says "one block every N minutes" (that is the solo
  expectation); it says the pool's blocks pay by PPLNS.

In v0.3.3 (testnet; no consensus changes):
- Header sync no longer stalls the node: the RandomX proof of work of each batch of headers is checked
  before the chain lock is taken, in parallel on up to 8 cores, instead of under the lock (a minute per
  2000 headers, during which wallet creation, the GUI and RPC calls waited; a wallet created during the
  first sync on the testnet stalled). The same headers are accepted and rejected as before.
- PQCH-Qt: while the block headers download, "Create a new wallet" and File > Create / Open / Restore
  wallet wait, with a line saying why and the progress; -minerapp makes its wallet when they finish.
- Linux: debug symbols in their own archive (pqch-0.3.3-x86_64-linux-gnu-debug.tar.gz), to read the
  stack of a program that hangs (not needed to run PQCH).

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
