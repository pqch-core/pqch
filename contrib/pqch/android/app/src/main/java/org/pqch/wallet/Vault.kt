// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

class WrongPinException : Exception("wrong PIN")
class LockedOutException(val seconds: Long) : Exception("too many wrong PINs: wait $seconds s")

/**
 * Where the recovery words live on the phone, encrypted twice:
 *  1. with a key derived from the PIN (PBKDF2-HMAC-SHA256, 200,000 rounds, random salt), so a wrong
 *     PIN cannot decrypt them (and nothing that checks the PIN is stored);
 *  2. with an AES key of the Android Keystore, which never leaves the phone's secure hardware, so the
 *     file copied off the phone cannot be attacked PIN by PIN.
 * After 5 wrong PINs, each try waits longer (30 s, 60 s, 120 s, …).
 * Public settings (network, server, the wallet id) live beside it.
 */
class Vault(context: Context) {
    private val prefs = context.getSharedPreferences("pqch_wallet", Context.MODE_PRIVATE)

    var network: String
        get() = prefs.getString("network", "test") ?: "test"
        set(v) = prefs.edit().putString("network", v).apply()

    var server: String
        get() = prefs.getString("server_$network", null) ?: defaultServer(network)
        set(v) = prefs.edit().putString("server_$network", v.trim()).apply()

    /**
     * The network has activated the ML-DSA emergency soft fork (contrib/pqch/SEGURANCA.md): coins can only
     * be spent through the SLH-DSA recovery key. Learnt from the node's answer to a payment; kept per network.
     */
    var mldsaDisabled: Boolean
        get() = prefs.getBoolean("mldsa_disabled_$network", false)
        set(v) = prefs.edit().putBoolean("mldsa_disabled_$network", v).apply()

    val hasWallet: Boolean get() = prefs.contains("blob")

    val walletId: String? get() = prefs.getString("wallet_id", null)

    fun store(keys: Keys, pin: String, walletId: String) {
        val salt = ByteArray(16).also { SecureRandom().nextBytes(it) }
        val secret = JSONObject().put("mnemonic", keys.mnemonic).put("passphrase", keys.passphrase).toString().toByteArray()
        val inner = gcmEncrypt(pinKey(pin, salt), secret)
        secret.fill(0)
        val outer = gcmEncrypt(keystoreKey(), inner)
        prefs.edit()
            .putString("blob", b64(outer))
            .putString("salt", b64(salt))
            .putString("wallet_id", walletId)
            .putInt("failures", 0)
            .putLong("locked_until", 0)
            .apply()
    }

    fun unlock(pin: String): Keys {
        val wait = (prefs.getLong("locked_until", 0) - System.currentTimeMillis()) / 1000
        if (wait > 0) throw LockedOutException(wait)
        val outer = unb64(prefs.getString("blob", null) ?: throw IllegalStateException("no wallet"))
        val salt = unb64(prefs.getString("salt", null)!!)
        val inner = gcmDecrypt(keystoreKey(), outer)
        val secret = try {
            gcmDecrypt(pinKey(pin, salt), inner)
        } catch (e: AEADBadTagException) {
            val failures = prefs.getInt("failures", 0) + 1
            val edit = prefs.edit().putInt("failures", failures)
            if (failures >= 5) edit.putLong("locked_until", System.currentTimeMillis() + 30_000L * (1L shl minOf(failures - 5, 10)))
            edit.apply()
            throw WrongPinException()
        }
        prefs.edit().putInt("failures", 0).putLong("locked_until", 0).apply()
        val json = JSONObject(String(secret))
        secret.fill(0)
        return Keys(json.getString("mnemonic"), json.optString("passphrase", ""))
    }

    /** Removes the wallet from this phone (the coins stay on the chain: the words restore them). */
    fun wipe() {
        prefs.edit().remove("blob").remove("salt").remove("wallet_id").remove("failures").remove("locked_until").apply()
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        if (ks.containsAlias(KEY_ALIAS)) ks.deleteEntry(KEY_ALIAS)
    }

    private fun pinKey(pin: String, salt: ByteArray): SecretKey {
        val spec = PBEKeySpec(pin.toCharArray(), salt, 200_000, 256)
        val bytes = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded
        spec.clearPassword()
        return SecretKeySpec(bytes, "AES")
    }

    private fun keystoreKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build()
        )
        return gen.generateKey()
    }

    private fun gcmEncrypt(key: SecretKey, data: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key)
        return cipher.iv + cipher.doFinal(data)
    }

    private fun gcmDecrypt(key: SecretKey, data: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, data, 0, 12))
        return cipher.doFinal(data, 12, data.size - 12)
    }

    private fun b64(b: ByteArray) = Base64.encodeToString(b, Base64.NO_WRAP)
    private fun unb64(s: String) = Base64.decode(s, Base64.NO_WRAP)

    companion object {
        private const val KEY_ALIAS = "pqch_wallet_words"

        fun defaultServer(network: String) = when (network) {
            "main" -> "https://explorer.pqch.org/api"
            // The explorer is for the real network only; the testnet has an API without pages.
            else -> "https://api-testnet.pqch.org/api"
        }
    }
}
