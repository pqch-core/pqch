// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.pqch.wallet

import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import java.text.DateFormat
import java.util.Date

// PQCH brand colours (as on pqch.org and in PQCH Core)
val Night = Color(0xFF151827)
val Card = Color(0xFF1F2337)
val Line = Color(0xFF2E3450)
val Ink = Color(0xFFF1F5F9)
val Muted = Color(0xFF94A3B8)
val Cyan = Color(0xFF22D3EE)
val Violet = Color(0xFF8B5CF6)
val Orange = Color(0xFFF97316)
val Pink = Color(0xFFDB2777)
val Green = Color(0xFF10B981)
val Red = Color(0xFFEF4444)
val HeroGradient = Brush.linearGradient(listOf(Color(0xFF1E1B4B), Color(0xFF3B2A6B), Color(0xFF7C3AED)))

@Composable
fun PqchTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Violet, onPrimary = Color.White, secondary = Cyan, onSecondary = Night,
            background = Night, onBackground = Ink, surface = Card, onSurface = Ink,
            surfaceVariant = Card, onSurfaceVariant = Muted, outline = Line, error = Red,
        ),
        content = content,
    )
}

@Composable
fun App(m: AppModel) {
    val s = m.screen
    val inside = s is Screen.Receive || s is Screen.Send || s is Screen.Review || s is Screen.Sent ||
        s is Screen.Settings || s is Screen.RevealWords
    BackHandler(enabled = inside) { m.go(Screen.Home) }
    BackHandler(enabled = s is Screen.ShowNewWords || s is Screen.VerifyWords || s is Screen.Restore ||
        (s is Screen.SetPin && !m.vault.hasWallet)) { m.go(Screen.Welcome) }

    Box(Modifier.fillMaxSize().background(Night).systemBarsPadding().imePadding()) {
        when (s) {
            Screen.Welcome -> WelcomeScreen(m)
            is Screen.ShowNewWords -> ShowWordsScreen(m, s.words)
            is Screen.VerifyWords -> VerifyScreen(m, s)
            Screen.Restore -> RestoreScreen(m)
            is Screen.SetPin -> SetPinScreen(m, s.keys)
            Screen.Unlock -> UnlockScreen(m)
            Screen.Home -> HomeScreen(m)
            Screen.Receive -> ReceiveScreen(m)
            Screen.Send -> SendScreen(m)
            is Screen.Review -> ReviewScreen(m, s)
            is Screen.Sent -> SentScreen(m, s.txid)
            Screen.Settings -> SettingsScreen(m)
            Screen.RevealWords -> RevealScreen(m)
        }
        if (m.busy) {
            Box(Modifier.fillMaxSize().background(Color(0x99000000)), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = Cyan)
            }
        }
    }
}

// ---------- building blocks ----------

@Composable
fun Page(title: String?, m: AppModel, back: (() -> Unit)? = null, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (back != null) {
                TextButton(onClick = back) { Text("‹ Back", color = Muted) }
                Spacer(Modifier.width(4.dp))
            }
            if (title != null) Text(title, color = Ink, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(12.dp))
        m.error?.let { ErrorBox(it) }
        content()
    }
}

@Composable
fun ErrorBox(text: String) {
    Text(
        text, color = Color(0xFFFECACA), fontSize = 14.sp,
        modifier = Modifier.fillMaxWidth().padding(bottom = 12.dp)
            .background(Color(0x33EF4444), RoundedCornerShape(10.dp))
            .border(1.dp, Color(0x88EF4444), RoundedCornerShape(10.dp)).padding(12.dp),
    )
}

@Composable
fun Note(text: String, color: Color = Muted) {
    Text(text, color = color, fontSize = 14.sp, modifier = Modifier.padding(bottom = 12.dp))
}

@Composable
fun Warning(text: String) {
    Text(
        text, color = Color(0xFFFED7AA), fontSize = 14.sp,
        modifier = Modifier.fillMaxWidth().padding(bottom = 12.dp)
            .background(Color(0x33F97316), RoundedCornerShape(10.dp))
            .border(1.dp, Color(0x88F97316), RoundedCornerShape(10.dp)).padding(12.dp),
    )
}

@Composable
fun Panel(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth().padding(bottom = 14.dp).clip(RoundedCornerShape(16.dp)).background(Card)
            .border(1.dp, Line, RoundedCornerShape(16.dp)).padding(16.dp),
        content = content,
    )
}

@Composable
fun Primary(text: String, enabled: Boolean = true, onClick: () -> Unit) {
    Button(
        onClick = onClick, enabled = enabled,
        modifier = Modifier.fillMaxWidth().height(52.dp).padding(bottom = 0.dp),
        shape = RoundedCornerShape(14.dp),
        colors = ButtonDefaults.buttonColors(containerColor = Violet, contentColor = Color.White),
    ) { Text(text, fontSize = 16.sp, fontWeight = FontWeight.Bold) }
    Spacer(Modifier.height(10.dp))
}

@Composable
fun Secondary(text: String, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick, modifier = Modifier.fillMaxWidth().height(50.dp), shape = RoundedCornerShape(14.dp),
    ) { Text(text, color = Ink, fontSize = 15.sp) }
    Spacer(Modifier.height(10.dp))
}

@Composable
fun Logo(size: Int = 72) {
    Image(painterResource(R.drawable.pqch_logo), contentDescription = "PQCH", modifier = Modifier.size(size.dp).clip(RoundedCornerShape((size / 5).dp)))
}

@Composable
fun PinField(label: String, value: String, onChange: (String) -> Unit) {
    OutlinedTextField(
        value = value, onValueChange = { v -> onChange(v.filter { it.isDigit() }.take(12)) },
        label = { Text(label) }, singleLine = true,
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
        modifier = Modifier.fillMaxWidth().padding(bottom = 10.dp),
    )
}

fun networkLabel(n: String) = if (n == "main") "MAINNET" else "TESTNET"

// ---------- create / restore ----------

@Composable
fun WelcomeScreen(m: AppModel) {
    Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Logo(96)
        Spacer(Modifier.height(16.dp))
        Text("PQCH Wallet", color = Ink, fontSize = 28.sp, fontWeight = FontWeight.Bold)
        Text("PostQuantum Cash · ${networkLabel(m.vault.network)}", color = Cyan, fontSize = 14.sp)
        Spacer(Modifier.height(12.dp))
        Text(
            "Send and receive PQCH. Your coins are protected by post-quantum signatures; your 24 recovery words never leave this phone.",
            color = Muted, fontSize = 15.sp, textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(28.dp))
        m.error?.let { ErrorBox(it) }
        Primary("Create a new wallet") { m.createWallet() }
        Secondary("Restore with 24 words") { m.go(Screen.Restore) }
        TextButton(onClick = { m.go(Screen.Settings) }) { Text("Network and server", color = Muted) }
    }
}

@Composable
fun ShowWordsScreen(m: AppModel, words: String) {
    Page("Your recovery words", m, back = { m.go(Screen.Welcome) }) {
        Warning("Write these 24 words on paper, in order, and keep them safe and offline. Anyone with them can take your coins. Never photograph them, never type them into a website, never send them to anyone — not even to PQCH.")
        Panel {
            val list = words.split(" ")
            for (row in 0 until 12) {
                Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                    for (col in 0..1) {
                        val i = row + col * 12
                        Row(Modifier.weight(1f)) {
                            Text("${i + 1}.".padStart(3), color = Muted, fontFamily = FontFamily.Monospace, fontSize = 15.sp)
                            Spacer(Modifier.width(6.dp))
                            Text(list[i], color = Ink, fontFamily = FontFamily.Monospace, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                        }
                    }
                }
            }
        }
        Note("The same words restore this wallet in PQCH Core on a computer, with the same addresses.")
        var done by remember { mutableStateOf(false) }
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(bottom = 12.dp)) {
            Checkbox(checked = done, onCheckedChange = { done = it })
            Text("I wrote them down on paper", color = Ink)
        }
        Primary("Continue", enabled = done) { m.startVerify(words) }
    }
}

@Composable
fun VerifyScreen(m: AppModel, s: Screen.VerifyWords) {
    val list = s.words.split(" ")
    val answers = remember(s) { s.positions.map { mutableStateOf("") } }
    var wrong by remember(s) { mutableStateOf(false) }
    Page("Check your copy", m, back = { m.go(Screen.ShowNewWords(s.words)) }) {
        Note("Type these words from your paper copy.")
        s.positions.forEachIndexed { k, pos ->
            OutlinedTextField(
                value = answers[k].value, onValueChange = { answers[k].value = it; wrong = false },
                label = { Text("Word ${pos + 1}") }, singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, autoCorrectEnabled = false, keyboardType = KeyboardType.Password),
                modifier = Modifier.fillMaxWidth().padding(bottom = 10.dp),
            )
        }
        if (wrong) ErrorBox("That does not match. Look at your paper copy again (or go back to see the words).")
        Primary("Check") {
            val ok = s.positions.withIndex().all { (k, pos) -> answers[k].value.trim().lowercase() == list[pos] }
            if (ok) m.go(Screen.SetPin(Keys(s.words, ""))) else wrong = true
        }
    }
}

@Composable
fun RestoreScreen(m: AppModel) {
    var words by remember { mutableStateOf("") }
    var passphrase by remember { mutableStateOf("") }
    var advanced by remember { mutableStateOf(false) }
    Page("Restore a wallet", m, back = { m.go(Screen.Welcome) }) {
        Note("Type your 24 recovery words, separated by spaces. A wallet from PQCH Core restores with the same addresses and balance.")
        OutlinedTextField(
            value = words, onValueChange = { words = it }, label = { Text("24 recovery words") },
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, autoCorrectEnabled = false, keyboardType = KeyboardType.Password),
            minLines = 4, modifier = Modifier.fillMaxWidth().padding(bottom = 10.dp),
        )
        Text("${words.trim().split(Regex("\\s+")).count { it.isNotEmpty() }} / 24 words", color = Muted, fontSize = 13.sp)
        Spacer(Modifier.height(8.dp))
        TextButton(onClick = { advanced = !advanced }) { Text(if (advanced) "Hide passphrase" else "I also used a passphrase", color = Cyan) }
        if (advanced) {
            OutlinedTextField(
                value = passphrase, onValueChange = { passphrase = it }, label = { Text("Passphrase (optional)") },
                singleLine = true, visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth().padding(bottom = 10.dp),
            )
            Note("Only if you chose one when the wallet was made. With a wrong passphrase the words open a different, empty wallet.")
        }
        Primary("Continue") { m.restore(words, passphrase) }
    }
}

@Composable
fun SetPinScreen(m: AppModel, keys: Keys) {
    var pin by remember { mutableStateOf("") }
    var again by remember { mutableStateOf("") }
    Page("Choose a PIN", m) {
        Note("The PIN opens the wallet on this phone. It encrypts your words here; it is not a replacement for them: if you lose the phone, only the 24 words bring the coins back.")
        PinField("PIN (6 to 12 digits)", pin) { pin = it }
        PinField("PIN again", again) { again = it }
        if (again.isNotEmpty() && again.length >= pin.length && again != pin) ErrorBox("The two PINs are different.")
        Primary("Save", enabled = pin.length >= 6 && pin == again) { m.savePin(keys, pin) }
    }
}

@Composable
fun UnlockScreen(m: AppModel) {
    var pin by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Logo(80)
        Spacer(Modifier.height(16.dp))
        Text("PQCH Wallet", color = Ink, fontSize = 24.sp, fontWeight = FontWeight.Bold)
        Text(networkLabel(m.vault.network), color = Cyan, fontSize = 13.sp)
        Spacer(Modifier.height(24.dp))
        m.error?.let { ErrorBox(it) }
        PinField("PIN", pin) { pin = it }
        Primary("Unlock", enabled = pin.length >= 6) { m.unlock(pin); pin = "" }
    }
}

// ---------- wallet ----------

@Composable
fun HomeScreen(m: AppModel) {
    val st = m.state
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Logo(40)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text("PQCH Wallet", color = Ink, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Text(networkLabel(m.vault.network), color = Cyan, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            }
            TextButton(onClick = { m.go(Screen.Settings) }) { Text("Settings", color = Muted) }
        }
        Spacer(Modifier.height(14.dp))
        m.error?.let { ErrorBox(it) }

        // Balance
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(HeroGradient).padding(20.dp)) {
            Text("BALANCE", color = Color(0xFFC4B5FD), fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text(
                if (st == null) "—" else formatPqch(st.confirmed),
                color = Color.White, fontSize = 32.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace,
            )
            Text("PQCH", color = Color(0xFF67E8F9), fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            if (st != null && st.pending != 0L) Text("Pending: ${formatPqch(st.pending)} PQCH", color = Color(0xFFE9D5FF), fontSize = 13.sp)
            if (st != null && st.immature != 0L) Text("Mined, not yet spendable: ${formatPqch(st.immature)} PQCH", color = Color(0xFFE9D5FF), fontSize = 13.sp)
        }
        Spacer(Modifier.height(16.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = { m.go(Screen.Receive) }, enabled = st != null, modifier = Modifier.weight(1f).height(52.dp),
                shape = RoundedCornerShape(14.dp), colors = ButtonDefaults.buttonColors(containerColor = Cyan, contentColor = Night),
            ) { Text("Receive", fontWeight = FontWeight.Bold, fontSize = 16.sp) }
            Button(
                onClick = { m.go(Screen.Send) }, enabled = st != null, modifier = Modifier.weight(1f).height(52.dp),
                shape = RoundedCornerShape(14.dp), colors = ButtonDefaults.buttonColors(containerColor = Violet, contentColor = Color.White),
            ) { Text("Send", fontWeight = FontWeight.Bold, fontSize = 16.sp) }
        }
        Spacer(Modifier.height(8.dp))
        TextButton(onClick = { m.refresh() }, modifier = Modifier.align(Alignment.CenterHorizontally)) { Text("↻ Refresh", color = Muted) }

        Text("Transactions", color = Ink, fontSize = 17.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 8.dp))
        if (st == null) {
            Note("Loading…")
        } else if (st.history.isEmpty()) {
            Note("No transactions yet. Tap Receive to get your address.")
        } else {
            val fmt = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT)
            Panel {
                st.history.forEachIndexed { i, h ->
                    if (i > 0) Box(Modifier.fillMaxWidth().height(1.dp).background(Line))
                    Row(Modifier.fillMaxWidth().padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                when {
                                    h.mined -> "Mined"
                                    h.net >= 0 -> "Received"
                                    else -> "Sent"
                                },
                                color = Ink, fontWeight = FontWeight.SemiBold,
                            )
                            Text(
                                if (!h.confirmed) "Unconfirmed" else h.time?.let { fmt.format(Date(it * 1000)) } ?: "",
                                color = if (h.confirmed) Muted else Orange, fontSize = 12.sp,
                            )
                        }
                        Text(
                            (if (h.net >= 0) "+" else "−") + formatPqch(kotlin.math.abs(h.net)),
                            color = if (h.net >= 0) Green else Pink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold,
                        )
                    }
                }
            }
            Note("Recent transactions of each address (up to 25 confirmed); the balance always counts every coin.")
        }
    }
}

fun qrBitmap(text: String, size: Int = 640): ImageBitmap {
    val matrix = QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, size, size, mapOf(EncodeHintType.MARGIN to 1))
    val pixels = IntArray(size * size) { i -> if (matrix.get(i % size, i / size)) 0xFF000000.toInt() else 0xFFFFFFFF.toInt() }
    val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
    bmp.setPixels(pixels, 0, size, 0, 0, size, size)
    return bmp.asImageBitmap()
}

@Composable
fun ReceiveScreen(m: AppModel) {
    val address = m.state?.receiveAddress ?: ""
    val clipboard = LocalClipboardManager.current
    var copied by remember { mutableStateOf(false) }
    Page("Receive", m, back = { m.go(Screen.Home) }) {
        Note("Give this address to whoever pays you. A new address is shown once this one has been used.")
        if (address.isNotEmpty()) {
            val qr = remember(address) { qrBitmap(address) }
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                Image(qr, contentDescription = "QR code of the address",
                    modifier = Modifier.size(260.dp).clip(RoundedCornerShape(16.dp)).background(Color.White).padding(10.dp))
            }
            Spacer(Modifier.height(16.dp))
            Panel {
                Text("YOUR ADDRESS", color = Muted, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                Text(address, color = Ink, fontFamily = FontFamily.Monospace, fontSize = 15.sp)
            }
            Primary(if (copied) "Copied ✓" else "Copy address") {
                clipboard.setText(AnnotatedString(address))
                copied = true
            }
        }
    }
}

@Composable
fun SendScreen(m: AppModel) {
    val clipboard = LocalClipboardManager.current
    var to by remember { mutableStateOf("") }
    var amount by remember { mutableStateOf("") }
    var sendAll by remember { mutableStateOf(false) }
    var speed by remember { mutableStateOf(1) } // 0 fast, 1 normal, 2 economy
    val fees = m.fees ?: FeeRates(2.0, 1.0, 1.0)
    val st = m.state
    val toError = remember(to) { if (to.isBlank()) null else Core.addressError(to.trim()) }
    val parsed = parsePqch(amount)
    LaunchedEffect(m.scanned) {
        m.scanned?.let { (address, amt) ->
            to = address
            if (amt != null) { amount = amt; sendAll = false }
            m.scanned = null
        }
    }
    Page("Send", m, back = { m.go(Screen.Home) }) {
        if (st != null) Note("Available: ${formatPqch(st.confirmed + st.pending)} PQCH")
        OutlinedTextField(
            value = to, onValueChange = { to = it }, label = { Text("Pay to (PQCH address)") },
            isError = toError != null, singleLine = true,
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, autoCorrectEnabled = false),
            modifier = Modifier.fillMaxWidth(),
        )
        if (toError != null) Text(toError, color = Red, fontSize = 13.sp)
        Row {
            TextButton(onClick = { clipboard.getText()?.let { to = it.text.trim() } }) { Text("Paste", color = Cyan) }
            TextButton(onClick = { m.scan() }) { Text("Scan QR code", color = Cyan) }
        }
        OutlinedTextField(
            value = if (sendAll) "whole balance" else amount, onValueChange = { amount = it }, enabled = !sendAll,
            label = { Text("Amount (PQCH)") }, singleLine = true,
            isError = !sendAll && amount.isNotBlank() && parsed == null,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
        )
        val everything = st?.let { formatPqch(it.confirmed + it.pending) }
        Row(verticalAlignment = Alignment.CenterVertically) {
            // Ticking it replaces any amount typed above: the whole balance goes, so the typed amount
            // is cleared to avoid the impression that only that amount will be sent.
            Checkbox(checked = sendAll, onCheckedChange = { sendAll = it; if (it) amount = "" })
            Text(
                "Send the whole balance" + (everything?.let { " ($it PQCH)" } ?: "") + " instead of an amount",
                color = Ink, fontSize = 14.sp,
            )
        }
        if (sendAll) Warning("Everything in this wallet will be sent" + (everything?.let { ": $it PQCH" } ?: "") +
            ", minus the fee. Untick the box to send only part of it.")
        Spacer(Modifier.height(8.dp))
        Text("Fee", color = Ink, fontWeight = FontWeight.SemiBold)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(vertical = 8.dp)) {
            listOf("Fast" to fees.fast, "Normal" to fees.normal, "Economy" to fees.economy).forEachIndexed { i, (name, rate) ->
                val selected = speed == i
                OutlinedButton(
                    onClick = { speed = i }, modifier = Modifier.weight(1f), shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.outlinedButtonColors(containerColor = if (selected) Color(0x448B5CF6) else Color.Transparent),
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(name, color = if (selected) Ink else Muted, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                        Text("${"%.1f".format(rate)}/vB", color = Muted, fontSize = 11.sp)
                    }
                }
            }
        }
        Note("Quanta per virtual byte. A PQCH payment is about 1 kvB per coin spent (post-quantum signatures are large).")
        val rate = when (speed) { 0 -> fees.fast; 2 -> fees.economy; else -> fees.normal }
        Primary("Review", enabled = to.isNotBlank() && toError == null && (sendAll || parsed != null)) {
            m.review(to, parsed, sendAll, rate)
        }
    }
}

@Composable
fun ReviewScreen(m: AppModel, s: Screen.Review) {
    val tx = s.tx
    val amount = tx.getLong("amount")
    val fee = tx.getLong("fee")
    Page(if (s.sendAll) "Confirm: send everything" else "Confirm the payment", m, back = { m.go(Screen.Send) }) {
        s.notice?.let { Warning(it) }
        if (s.sendAll) Warning("You are sending the WHOLE BALANCE of this wallet. The wallet will be empty afterwards.")
        Panel {
            Text("TO", color = Muted, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text(s.to, color = Ink, fontFamily = FontFamily.Monospace, fontSize = 14.sp)
            Spacer(Modifier.height(12.dp))
            ReviewLine(if (s.sendAll) "Amount (everything)" else "Amount", "${formatPqch(amount)} PQCH", Ink)
            ReviewLine("Fee", "${formatPqch(fee)} PQCH", Muted)
            ReviewLine("Size", "${tx.getLong("vsize")} vB", Muted)
            if (tx.optBoolean("recovery")) ReviewLine("Signed with", "recovery key (SLH-DSA)", Muted)
            Box(Modifier.fillMaxWidth().padding(vertical = 8.dp).height(1.dp).background(Line))
            ReviewLine("Total", "${formatPqch(amount + fee)} PQCH", Cyan)
        }
        Warning("Check the address character by character. A payment cannot be undone.")
        Primary("Send now") { m.broadcast(tx) }
        Secondary("Cancel") { m.go(Screen.Home) }
    }
}

@Composable
fun ReviewLine(label: String, value: String, color: Color) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(label, color = Muted, modifier = Modifier.weight(1f))
        Text(value, color = color, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
fun SentScreen(m: AppModel, txid: String) {
    val clipboard = LocalClipboardManager.current
    Page("Sent", m) {
        Text("✓", color = Green, fontSize = 56.sp, modifier = Modifier.align(Alignment.CenterHorizontally))
        Note("The payment was sent to the network. It confirms in about a minute.", Ink)
        Panel {
            Text("TRANSACTION ID", color = Muted, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text(txid, color = Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
        }
        Secondary("Copy transaction id") { clipboard.setText(AnnotatedString(txid)) }
        Primary("Done") { m.go(Screen.Home) }
    }
}

// ---------- settings ----------

@Composable
fun SettingsScreen(m: AppModel) {
    var network by remember { mutableStateOf(m.vault.network) }
    var server by remember { mutableStateOf(m.vault.server) }
    var askPin by remember { mutableStateOf(false) }
    var askWipe by remember { mutableStateOf(false) }
    var pin by remember { mutableStateOf("") }
    val hasWallet = m.vault.hasWallet
    Page("Settings", m, back = { m.go(if (m.keys != null) Screen.Home else if (hasWallet) Screen.Unlock else Screen.Welcome) }) {
        // Which build is installed (the build number grows with every APK, so an update can be checked).
        Note("PQCH Wallet ${BuildConfig.VERSION_NAME} (build ${BuildConfig.VERSION_CODE})")
        Panel {
            Text("Network", color = Ink, fontWeight = FontWeight.SemiBold)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(vertical = 8.dp)) {
                listOf("test" to "Testnet", "main" to "Mainnet").forEach { (id, name) ->
                    OutlinedButton(
                        onClick = { network = id; server = m.vault.let { v -> if (id == v.network) v.server else Vault.defaultServer(id) } },
                        modifier = Modifier.weight(1f), shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.outlinedButtonColors(containerColor = if (network == id) Color(0x448B5CF6) else Color.Transparent),
                    ) { Text(name, color = if (network == id) Ink else Muted) }
                }
            }
            Note("The same words give different addresses on each network. Mainnet starts at launch.")
            OutlinedTextField(
                value = server, onValueChange = { server = it }, label = { Text("Server (explorer API, https)") }, singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, autoCorrectEnabled = false, keyboardType = KeyboardType.Uri),
                modifier = Modifier.fillMaxWidth(),
            )
            Note("The server sees which addresses this wallet asks about, never the words. You can use your own server.")
            Primary("Save") {
                m.setServer(network, server)
                m.go(if (m.keys != null) Screen.Home else if (hasWallet) Screen.Unlock else Screen.Welcome)
            }
        }
        if (hasWallet) {
            Panel {
                Text("Recovery words", color = Ink, fontWeight = FontWeight.SemiBold)
                Note("Show the 24 words to make a new paper copy. Make sure nobody is watching.")
                if (askPin) {
                    PinField("PIN", pin) { pin = it }
                    Primary("Show words", enabled = pin.length >= 6) { m.checkPin(pin) { m.go(Screen.RevealWords) }; pin = "" }
                } else {
                    Secondary("Show recovery words") { askPin = true }
                }
            }
            Panel {
                Text("Remove the wallet from this phone", color = Ink, fontWeight = FontWeight.SemiBold)
                Note("The coins stay on the network; only the 24 words bring them back.")
                OutlinedButton(onClick = { askWipe = true }, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp)) {
                    Text("Remove wallet", color = Red)
                }
            }
        }
        Note("PQCH Wallet ${BuildConfig.VERSION_NAME} · test version")
    }
    if (askWipe) {
        AlertDialog(
            onDismissRequest = { askWipe = false },
            title = { Text("Remove the wallet?") },
            text = { Text("Without your 24 recovery words written down, the coins are lost for ever. Remove only if you have them.") },
            confirmButton = { TextButton(onClick = { askWipe = false; m.wipe() }) { Text("Remove", color = Red) } },
            dismissButton = { TextButton(onClick = { askWipe = false }) { Text("Cancel") } },
        )
    }
}

@Composable
fun RevealScreen(m: AppModel) {
    val words = m.keys?.mnemonic
    Page("Recovery words", m, back = { m.go(Screen.Home) }) {
        Warning("Anyone who sees these words can take your coins.")
        if (words != null) {
            Panel {
                words.split(" ").forEachIndexed { i, w ->
                    Text("${(i + 1).toString().padStart(2)}. $w", color = Ink, fontFamily = FontFamily.Monospace, fontSize = 16.sp)
                }
            }
            if (m.keys?.passphrase?.isNotEmpty() == true) Note("This wallet also uses a passphrase (not shown).")
        } else {
            Note("Unlock the wallet first.")
        }
        Primary("Done") { m.go(Screen.Home) }
    }
}
