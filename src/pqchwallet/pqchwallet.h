// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PQCH_PQCHWALLET_H
#define PQCH_PQCHWALLET_H

/**
 * PQCH light wallet core (contrib/pqch/CARTEIRA-LEVE.md): the keys and signatures of a PQCH Core
 * wallet, for a wallet that keeps no chain (the Android app). It gives the same addresses as
 * PQCH Core for the same 24 recovery words, and signs P2MR transactions with the same code.
 * It never talks to the network: the app reads coins from and sends transactions to an
 * Esplora-style API (pqch-electrs).
 *
 * One C entry point, JSON in and out, so that the Android (JNI) layer stays a few lines:
 *
 *   char* out = pqchw_call("addresses", "{\"mnemonic\": \"...\", \"branch\": 0, \"from\": 0, \"count\": 20}");
 *   ... read {"ok": true, "result": ...} or {"ok": false, "error": "..."} ...
 *   pqchw_free(out);
 *
 * Methods (params → result):
 *   init           {chain: "main"|"test"|"regtest"} → {chain, coinbase_maturity, block_seconds}           first, and when changing network
 *   new_mnemonic   {} → {mnemonic}                                     24 new words (strong randomness)
 *   check_mnemonic {mnemonic} → {valid, normalized}
 *   wallet_id      {mnemonic, passphrase?} → {id}                       public id of the seed (no secret)
 *   addresses      {mnemonic, passphrase?, branch: 0 receive|1 change, from, count}
 *                  → [{index, address, script}]
 *   check_address  {address} → {valid, script?, error?}
 *   create_tx      {mnemonic, passphrase?, utxos: [{txid, vout, value, branch, index}],
 *                   to, amount, fee_rate (quanta/vB), change_index, subtract_fee?, send_all?, locktime?}
 *                  → {hex, txid, fee, vsize, amount, inputs: ["txid:vout"], change: {address, value}|null}
 * Amounts are integers in quanta (1 PQCH = 100,000,000 quanta).
 *
 * Calls are serialised: it is safe to call from several threads.
 */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define PQCHW_EXPORT __declspec(dllexport)
#else
#define PQCHW_EXPORT __attribute__((visibility("default")))
#endif

/** Run `method` with `params_json` (UTF-8). Returns a JSON string that must be freed with pqchw_free. */
PQCHW_EXPORT char* pqchw_call(const char* method, const char* params_json);
/** Free a string returned by pqchw_call (its memory is wiped first: it may hold words). */
PQCHW_EXPORT void pqchw_free(char* str);

#ifdef __cplusplus
}
#endif

#endif // PQCH_PQCHWALLET_H
