// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import org.json.JSONArray
import org.json.JSONObject

class CoreException(message: String) : Exception(message)

/**
 * The light wallet core (libpqchwallet.so, src/pqchwallet in the PQCH repository): the same keys and
 * signatures as PQCH Core. One native function, JSON in and out.
 */
object Core {
    init {
        System.loadLibrary("pqchwallet")
    }

    @JvmStatic
    external fun call(method: String, params: String): String

    private fun run(method: String, params: JSONObject): Any {
        val reply = JSONObject(call(method, params.toString()))
        if (!reply.optBoolean("ok")) throw CoreException(reply.optString("error", "unknown error"))
        return reply.get("result")
    }

    fun init(chain: String): JSONObject = run("init", JSONObject().put("chain", chain)) as JSONObject

    fun newMnemonic(): String = (run("new_mnemonic", JSONObject()) as JSONObject).getString("mnemonic")

    /** The words in canonical form, or null if they are not 24 valid words. */
    fun normalizeMnemonic(words: String): String? {
        val r = run("check_mnemonic", JSONObject().put("mnemonic", words)) as JSONObject
        return if (r.optBoolean("valid")) r.getString("normalized") else null
    }

    fun walletId(keys: Keys): String =
        (run("wallet_id", keys.json()) as JSONObject).getString("id")

    fun addresses(keys: Keys, branch: Int, from: Int, count: Int): List<String> {
        val list = run("addresses", keys.json().put("branch", branch).put("from", from).put("count", count)) as JSONArray
        return (0 until list.length()).map { list.getJSONObject(it).getString("address") }
    }

    /** Null if valid, otherwise why not. */
    fun addressError(address: String): String? {
        val r = run("check_address", JSONObject().put("address", address)) as JSONObject
        return if (r.optBoolean("valid")) null else r.optString("error", "invalid address")
    }

    fun createTx(keys: Keys, request: JSONObject): JSONObject {
        val params = keys.json()
        for (k in request.keys()) params.put(k, request.get(k))
        return run("create_tx", params) as JSONObject
    }
}

/** The wallet's secrets while it is unlocked. */
class Keys(val mnemonic: String, val passphrase: String) {
    fun json(): JSONObject = JSONObject().put("mnemonic", mnemonic).put("passphrase", passphrase)
}
