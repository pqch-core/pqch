// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

class ApiException(message: String) : IOException(message)

/**
 * The explorer's API (pqch-electrs, Esplora style) at `base`, e.g. https://explorer.pqch.org/testnet/api.
 * Only HTTPS (network_security_config.xml).
 */
class Api(base: String) {
    private val base = base.trimEnd('/')

    private fun open(path: String): HttpURLConnection {
        val c = URL(base + path).openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 30_000
        c.setRequestProperty("Accept", "application/json, text/plain")
        return c
    }

    private fun read(c: HttpURLConnection): String {
        val code = c.responseCode
        val stream = if (code in 200..299) c.inputStream else c.errorStream
        val body = stream?.bufferedReader()?.use { it.readText() } ?: ""
        c.disconnect()
        if (code !in 200..299) throw ApiException("server answered $code: ${body.take(300)}")
        return body
    }

    fun get(path: String): String = read(open(path))

    /** Broadcasts a signed transaction; returns its txid. */
    fun broadcast(hex: String): String {
        val c = open("/tx")
        c.requestMethod = "POST"
        c.doOutput = true
        c.setRequestProperty("Content-Type", "text/plain")
        c.outputStream.use { it.write(hex.toByteArray()) }
        return read(c).trim()
    }
}
