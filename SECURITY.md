# Security Policy

## Reporting a vulnerability

**Do not report security problems in public issues, pull requests, chats or social networks.**
Report them privately, in one of these two ways:

1. **GitHub private vulnerability reporting:** in this repository, open the **Security** tab and
   choose **Report a vulnerability**
   (<https://github.com/pqch-core/pqch/security/advisories/new>).
2. **Email:** write to **community@pqch.org** with the subject **`security`**.

Please include:

- the PQCH version (`pqchd -version`) and the network (real network, testnet or regtest);
- what is affected (consensus, P2P, wallet, RPC, post-quantum signatures, merged mining, …);
- the steps to reproduce it and what you expected instead;
- whether you have told anyone else.

Do not send recovery words, private keys or wallet files, yours or anyone else's: they are never
needed to report a problem.

## What happens next

- We confirm that we received the report.
- We work on a fix privately and tell you how it is going.
- The fix is released first; the details are published afterwards, giving credit to the person who
  reported it (unless you prefer not to be named).

Problems that only affect the testnet are also welcome: the testnet exists to find them before the
real network does.

## Supported versions

Only the latest PQCH release receives security fixes. PQCH is a fork of Bitcoin Core; problems that
also affect Bitcoin Core should additionally be reported to Bitcoin Core as described in its own
security policy (<https://bitcoincore.org/en/contact/>).
