PQCH Core
=========

Intro
-----
PQCH (PostQuantum Cash) is a free, open source peer-to-peer electronic cash
system built on Bitcoin Core, with post-quantum signatures (ML-DSA-44, with
SLH-DSA-128s as a recovery path) and P2MR addresses (pqc1p...).

Setup
-----
Run pqch-qt.exe. On first start PQCH Core creates a wallet and shows its
24 recovery words: write them on paper and keep them safe and offline.
Anyone with the 24 words has your funds. PQCH will never ask for them.

PQCH Core downloads and checks the entire history of PQCH transactions;
depending on your computer and connection this can take a while.

Verify every download against the signed SHA256SUMS published at:
  https://pqch.org/
