// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import com.google.zxing.client.android.Intents
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions

class MainActivity : ComponentActivity() {
    private lateinit var model: AppModel

    // Camera QR reader (Send screen). The reader is a separate screen, so the wallet is not locked
    // while it is open; if the person leaves from the reader, it locks on return after 2 minutes.
    private val scanLauncher: ActivityResultLauncher<ScanOptions> = registerForActivityResult(ScanContract()) { r ->
        model.scanFinished(r.contents)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // No screenshots, no screen recording, no preview in the recent apps list: the words and
        // balances are never captured.
        window.setFlags(WindowManager.LayoutParams.FLAG_SECURE, WindowManager.LayoutParams.FLAG_SECURE)
        model = AppModel(Vault(applicationContext))
        model.launchScanner = {
            scanLauncher.launch(
                ScanOptions()
                    .setDesiredBarcodeFormats(ScanOptions.QR_CODE)
                    .setPrompt("Point the camera at a PQCH address")
                    .setBeepEnabled(false)
                    .setOrientationLocked(false)
                    .addExtra(Intents.Scan.SCAN_TYPE, Intents.Scan.MIXED_SCAN)
            )
        }
        setContent { PqchTheme { App(model) } }
    }

    override fun onStart() {
        super.onStart()
        model.checkScanTimeout()
    }

    override fun onStop() {
        super.onStop()
        // Leaving the app locks the wallet: the words are forgotten until the PIN is entered again.
        // Opening the QR reader is not leaving the app.
        if (!model.scanning) model.lock()
    }
}

sealed interface Screen {
    data object Welcome : Screen
    data class ShowNewWords(val words: String) : Screen
    data class VerifyWords(val words: String, val positions: List<Int>) : Screen
    data object Restore : Screen
    data class SetPin(val keys: Keys) : Screen
    data object Unlock : Screen
    data object Home : Screen
    data object Receive : Screen
    data object Send : Screen
    data class Review(val tx: JSONObject, val to: String) : Screen
    data class Sent(val txid: String) : Screen
    data object Settings : Screen
    data object RevealWords : Screen
}

class AppModel(val vault: Vault) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    var screen by mutableStateOf<Screen>(if (vault.hasWallet) Screen.Unlock else Screen.Welcome)
    var keys: Keys? = null
        private set
    var state by mutableStateOf<WalletState?>(null)
    var fees by mutableStateOf<FeeRates?>(null)
    var busy by mutableStateOf(false)
    var error by mutableStateOf<String?>(null)
    var coinbaseMaturity = 100
        private set

    init {
        initCore()
    }

    private fun initCore() {
        try {
            coinbaseMaturity = Core.init(vault.network).getInt("coinbase_maturity")
        } catch (e: Exception) {
            error = e.message
        }
    }

    val api: Api get() = Api(vault.server)

    /** Runs `work` off the main thread with a busy indicator; errors go to `error`. */
    fun task(work: suspend () -> Unit) {
        if (busy) return
        busy = true
        error = null
        scope.launch {
            try {
                work()
            } catch (e: WrongPinException) {
                error = "Wrong PIN."
            } catch (e: LockedOutException) {
                error = "Too many wrong PINs. Try again in ${e.seconds} seconds."
            } catch (e: java.io.IOException) {
                error = "Cannot reach the server (${vault.server}): ${e.message}"
            } catch (e: Exception) {
                error = e.message ?: e.toString()
            } finally {
                busy = false
            }
        }
    }

    // QR reader
    var launchScanner: (() -> Unit)? = null
    var scanning = false
        private set
    private var scanStartedAt = 0L
    /** The last QR read on the Send screen: (address, amount or null). */
    var scanned by mutableStateOf<Pair<String, String?>?>(null)

    fun scan() {
        val launch = launchScanner ?: return
        scanning = true
        scanStartedAt = System.currentTimeMillis()
        error = null
        launch()
    }

    fun scanFinished(contents: String?) {
        scanning = false
        if (contents == null) return
        val p = parsePaymentQr(contents)
        if (p == null) error = "This QR code is not a PQCH address." else scanned = p
    }

    fun checkScanTimeout() {
        if (scanning && System.currentTimeMillis() - scanStartedAt > 120_000) {
            scanning = false
            lock()
        }
    }

    fun go(s: Screen) {
        error = null
        screen = s
    }

    fun lock() {
        keys = null
        state = null
        if (vault.hasWallet) go(Screen.Unlock)
    }

    fun createWallet() = task {
        val words = withContext(Dispatchers.Default) { Core.newMnemonic() }
        go(Screen.ShowNewWords(words))
    }

    fun startVerify(words: String) {
        val positions = (0 until 24).shuffled().take(3).sorted()
        go(Screen.VerifyWords(words, positions))
    }

    fun restore(words: String, passphrase: String) = task {
        val normalized = withContext(Dispatchers.Default) { Core.normalizeMnemonic(words) }
            ?: throw CoreException("These are not 24 valid recovery words. Check each word and its order.")
        go(Screen.SetPin(Keys(normalized, passphrase)))
    }

    fun savePin(k: Keys, pin: String) = task {
        withContext(Dispatchers.Default) {
            val id = Core.walletId(k)
            vault.store(k, pin, id)
        }
        keys = k
        go(Screen.Home)
        refreshNow()
    }

    fun unlock(pin: String) = task {
        keys = withContext(Dispatchers.Default) { vault.unlock(pin) }
        go(Screen.Home)
        refreshNow()
    }

    fun refresh() = task { refreshNow() }

    private suspend fun refreshNow() {
        val k = keys ?: return
        val api = this.api
        val maturity = coinbaseMaturity
        val result = withContext(Dispatchers.IO) { WalletSync(k, api, maturity).sync() to fetchFees(api) }
        state = result.first
        fees = result.second
    }

    fun review(to: String, amount: Long?, sendAll: Boolean, feeRate: Double) = task {
        val k = keys ?: return@task
        val s = state ?: throw CoreException("Refresh the balance first.")
        val coins = s.coins.filter { !it.immature }
        if (coins.isEmpty()) throw CoreException("No coins to spend yet.")
        val request = JSONObject()
            .put("utxos", org.json.JSONArray(coins.map {
                JSONObject().put("txid", it.txid).put("vout", it.vout).put("value", it.value)
                    .put("branch", it.branch).put("index", it.index)
            }))
            .put("to", to.trim())
            .put("fee_rate", feeRate)
            .put("change_index", s.changeIndex)
            .put("send_all", sendAll)
            .put("locktime", s.tipHeight)
        if (!sendAll) request.put("amount", amount ?: throw CoreException("Enter the amount."))
        val tx = withContext(Dispatchers.Default) { Core.createTx(k, request) }
        go(Screen.Review(tx, to.trim()))
    }

    fun broadcast(tx: JSONObject) = task {
        val api = this.api
        val txid = withContext(Dispatchers.IO) { api.broadcast(tx.getString("hex")) }
        go(Screen.Sent(txid))
        try {
            refreshNow()
        } catch (e: Exception) {
            // the payment went out; the balance updates on the next refresh
        }
    }

    fun setServer(network: String, server: String) {
        vault.network = network
        if (server.isNotBlank()) vault.server = server
        initCore()
        state = null
        refresh()
    }

    fun checkPin(pin: String, then: () -> Unit) = task {
        withContext(Dispatchers.Default) { vault.unlock(pin) }
        then()
    }

    fun wipe() {
        vault.wipe()
        keys = null
        state = null
        go(Screen.Welcome)
    }
}
