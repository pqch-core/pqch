// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.math.RoundingMode

const val COIN = 100_000_000L

/** Unused addresses in a row after which a branch is considered fully scanned (as most wallets do). */
const val GAP_LIMIT = 20

data class Coin(
    val txid: String,
    val vout: Int,
    val value: Long,
    val branch: Int,
    val index: Int,
    val confirmed: Boolean,
    val immature: Boolean,
)

data class HistoryItem(
    val txid: String,
    val net: Long,          // what the wallet gained (> 0) or lost (< 0, fee included)
    val confirmed: Boolean,
    val time: Long?,        // block time, seconds
    val mined: Boolean,
)

data class WalletState(
    val confirmed: Long,
    val pending: Long,
    val immature: Long,
    val coins: List<Coin>,
    val history: List<HistoryItem>,
    val receiveAddress: String,
    val changeIndex: Int,
    val tipHeight: Int,
)

/** Reads the wallet's coins and history from the explorer API. Addresses are derived by the core. */
class WalletSync(private val keys: Keys, private val api: Api, private val coinbaseMaturity: Int) {
    private val derived = HashMap<Pair<Int, Int>, List<String>>() // (branch, batch start) → addresses

    private fun batch(branch: Int, from: Int): List<String> =
        derived.getOrPut(branch to from) { Core.addresses(keys, branch, from, GAP_LIMIT) }

    fun sync(): WalletState {
        val tip = api.get("/blocks/tip/height").trim().toInt()
        val mine = HashMap<String, Pair<Int, Int>>() // address → (branch, index)
        val used = ArrayList<String>()
        var receiveAddress = ""
        var changeIndex = 0
        for (branch in 0..1) {
            var from = 0
            var gap = 0
            var firstUnused = -1
            while (gap < GAP_LIMIT) {
                val addrs = batch(branch, from)
                for ((i, a) in addrs.withIndex()) {
                    val index = from + i
                    mine[a] = branch to index
                    val info = JSONObject(api.get("/address/$a"))
                    val txs = info.getJSONObject("chain_stats").getInt("tx_count") +
                        info.getJSONObject("mempool_stats").getInt("tx_count")
                    if (txs > 0) {
                        used.add(a)
                        gap = 0
                        firstUnused = -1
                    } else {
                        if (firstUnused < 0) firstUnused = index
                        if (++gap >= GAP_LIMIT) break
                    }
                }
                from += GAP_LIMIT
            }
            if (branch == 0) receiveAddress = batch(0, firstUnused / GAP_LIMIT * GAP_LIMIT)[firstUnused % GAP_LIMIT]
            else changeIndex = firstUnused
        }

        // Transactions and coins of the used addresses.
        val txs = LinkedHashMap<String, JSONObject>()
        val coins = ArrayList<Coin>()
        for (a in used) {
            val list = JSONArray(api.get("/address/$a/txs"))
            for (i in 0 until list.length()) {
                val tx = list.getJSONObject(i)
                txs[tx.getString("txid")] = tx
            }
            val utxos = JSONArray(api.get("/address/$a/utxo"))
            val (branch, index) = mine.getValue(a)
            for (i in 0 until utxos.length()) {
                val u = utxos.getJSONObject(i)
                val txid = u.getString("txid")
                val status = u.getJSONObject("status")
                val confirmed = status.optBoolean("confirmed")
                val tx = txs[txid] ?: JSONObject(api.get("/tx/$txid")).also { txs[txid] = it }
                val coinbase = isCoinbase(tx)
                val depth = if (confirmed) tip - status.getInt("block_height") + 1 else 0
                coins.add(
                    Coin(txid, u.getInt("vout"), u.getLong("value"), branch, index, confirmed,
                        immature = coinbase && depth < coinbaseMaturity)
                )
            }
        }

        val history = txs.values.map { tx ->
            var received = 0L
            var sent = 0L
            val vout = tx.getJSONArray("vout")
            for (i in 0 until vout.length()) {
                val o = vout.getJSONObject(i)
                if (o.optString("scriptpubkey_address") in mine) received += o.getLong("value")
            }
            val vin = tx.getJSONArray("vin")
            for (i in 0 until vin.length()) {
                val prev = vin.getJSONObject(i).optJSONObject("prevout") ?: continue
                if (prev.optString("scriptpubkey_address") in mine) sent += prev.getLong("value")
            }
            val status = tx.getJSONObject("status")
            HistoryItem(
                txid = tx.getString("txid"),
                net = received - sent,
                confirmed = status.optBoolean("confirmed"),
                time = if (status.has("block_time")) status.getLong("block_time") else null,
                mined = isCoinbase(tx),
            )
        }.sortedWith(compareBy<HistoryItem> { it.confirmed }.thenByDescending { it.time ?: Long.MAX_VALUE })

        return WalletState(
            confirmed = coins.filter { it.confirmed && !it.immature }.sumOf { it.value },
            pending = coins.filter { !it.confirmed }.sumOf { it.value },
            immature = coins.filter { it.immature }.sumOf { it.value },
            coins = coins,
            history = history,
            receiveAddress = receiveAddress,
            changeIndex = changeIndex,
            tipHeight = tip,
        )
    }

    private fun isCoinbase(tx: JSONObject): Boolean {
        val vin = tx.getJSONArray("vin")
        return vin.length() > 0 && vin.getJSONObject(0).optBoolean("is_coinbase")
    }
}

/** Fee rates in quanta per vB: the explorer's mempool backend if there is one, else the index's own
 *  estimates (pqch-electrs `/fee-estimates`, blocks → rate), else sane minimums. */
data class FeeRates(val fast: Double, val normal: Double, val economy: Double)

fun fetchFees(api: Api): FeeRates = try {
    val j = JSONObject(api.get("/v1/fees/recommended"))
    FeeRates(
        fast = j.optDouble("fastestFee", 2.0).coerceAtLeast(1.0),
        normal = j.optDouble("halfHourFee", 1.0).coerceAtLeast(1.0),
        economy = j.optDouble("economyFee", 1.0).coerceAtLeast(1.0),
    )
} catch (e: Exception) {
    try {
        val j = JSONObject(api.get("/fee-estimates"))
        // 1 block is a minute on PQCH: 1 / 30 / 144 blocks for fast / normal / economy.
        fun at(blocks: Int, fallback: Double): Double {
            val k = j.keys().asSequence().mapNotNull { it.toIntOrNull() }.filter { it >= blocks }.minOrNull()
            return (if (k == null) fallback else j.optDouble(k.toString(), fallback)).coerceAtLeast(1.0)
        }
        FeeRates(fast = at(1, 2.0), normal = at(30, 1.0), economy = at(144, 1.0))
    } catch (e2: Exception) {
        FeeRates(2.0, 1.0, 1.0)
    }
}

/** 123456789 → "1.23456789". */
fun formatPqch(quanta: Long): String =
    BigDecimal.valueOf(quanta).movePointLeft(8).setScale(8, RoundingMode.UNNECESSARY).toPlainString()

/** "1.5" → 150000000; null if not a valid amount (more than 8 decimals, negative, …). */
fun parsePqch(text: String): Long? = try {
    val v = BigDecimal(text.trim().replace(',', '.'))
    if (v.signum() <= 0 || v.scale() > 8) null else v.movePointRight(8).longValueExact()
} catch (e: Exception) {
    null
}
