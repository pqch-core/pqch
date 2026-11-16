#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
"""PQCH testnet faucet (torneira).

A small web page that sends a fixed amount of test coins to an address. Python standard library
only; runs behind nginx (which does TLS) and talks to a local pqchd wallet over RPC with a user
that may only call the few wallet commands listed in README.md.

Limits, all checked and recorded under one lock so two requests cannot both pass:
  - one payout per address and one per IP address per PERIOD (default 24 h);
  - at most DAILY_CAP coins in any PERIOD, and at most BURST payouts per minute.
IP addresses are stored only as salted hashes and are forgotten after two periods.

Configuration (environment):
  RPC_URL       http://127.0.0.1:39332/wallet/torneira
  RPC_USER      faucet
  RPC_PASSWORD  (required)
  AMOUNT        4            coins per payout
  DAILY_CAP     400          coins per PERIOD for everyone together
  BURST         10           payouts per minute for everyone together
  PERIOD        86400        seconds
  LISTEN        127.0.0.1:3010
  DB            /var/lib/pqch-faucet/faucet.sqlite
  TRUST_PROXY   1            take the client IP from X-Real-IP (set by nginx); 0 for direct tests
"""

import base64
import hashlib
import html
import json
import os
import secrets
import sqlite3
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from decimal import Decimal
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RPC_URL = os.environ.get("RPC_URL", "http://127.0.0.1:39332/wallet/torneira")
RPC_USER = os.environ.get("RPC_USER", "faucet")
RPC_PASSWORD = os.environ.get("RPC_PASSWORD", "")
AMOUNT = Decimal(os.environ.get("AMOUNT", "4"))
DAILY_CAP = Decimal(os.environ.get("DAILY_CAP", "400"))
BURST = int(os.environ.get("BURST", "10"))
PERIOD = int(os.environ.get("PERIOD", "86400"))
LISTEN = os.environ.get("LISTEN", "127.0.0.1:3010")
DB_PATH = os.environ.get("DB", "/var/lib/pqch-faucet/faucet.sqlite")
TRUST_PROXY = os.environ.get("TRUST_PROXY", "1") == "1"
MAX_BODY = 2048

LOCK = threading.Lock()


class RPCError(Exception):
    pass


def rpc(method, *params):
    body = json.dumps({"jsonrpc": "1.0", "id": "faucet", "method": method, "params": list(params)}).encode()
    req = urllib.request.Request(RPC_URL, data=body, headers={"Content-Type": "application/json"})
    token = base64.b64encode(f"{RPC_USER}:{RPC_PASSWORD}".encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            reply = json.loads(r.read(), parse_float=Decimal)
    except urllib.error.HTTPError as e:
        try:
            reply = json.loads(e.read(), parse_float=Decimal)
        except Exception:
            raise RPCError(f"node answered HTTP {e.code}") from None
    except OSError as e:
        raise RPCError(f"node unreachable: {e}") from None
    if reply.get("error"):
        raise RPCError(reply["error"].get("message", "error"))
    return reply["result"]


def db():
    con = sqlite3.connect(DB_PATH, timeout=10)
    con.execute("CREATE TABLE IF NOT EXISTS payouts (t INTEGER, address TEXT, iphash TEXT, amount TEXT, txid TEXT)")
    con.execute("CREATE INDEX IF NOT EXISTS payouts_t ON payouts (t)")
    con.execute("CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT)")
    if con.execute("SELECT v FROM meta WHERE k='salt'").fetchone() is None:
        con.execute("INSERT INTO meta VALUES ('salt', ?)", (secrets.token_hex(32),))
    con.commit()
    return con


def ip_hash(con, ip):
    salt = con.execute("SELECT v FROM meta WHERE k='salt'").fetchone()[0]
    return hashlib.sha256((salt + ip).encode()).hexdigest()


def request_payout(address, ip):
    """Returns (ok, message). Sends AMOUNT to address if every limit allows it."""
    address = address.strip()
    if not address or len(address) > 120 or not address.isalnum():
        return False, "Enter a PQCH testnet address (pqct1…)."
    try:
        info = rpc("validateaddress", address)
    except RPCError as e:
        return False, f"The faucet is unavailable right now ({e})."
    if not info.get("isvalid"):
        return False, "This is not a valid PQCH testnet address. Testnet addresses start with pqct1."
    address = info.get("address", address)

    with LOCK:
        con = db()
        try:
            now = int(time.time())
            con.execute("DELETE FROM payouts WHERE t < ?", (now - 2 * PERIOD,))
            con.commit()
            since = now - PERIOD
            h = ip_hash(con, ip)
            if con.execute("SELECT 1 FROM payouts WHERE t >= ? AND address = ?", (since, address)).fetchone():
                return False, "This address already received test coins in the last 24 hours."
            if con.execute("SELECT 1 FROM payouts WHERE t >= ? AND iphash = ?", (since, h)).fetchone():
                return False, "You already received test coins in the last 24 hours. Come back tomorrow."
            given = sum((Decimal(a) for (a,) in con.execute("SELECT amount FROM payouts WHERE t >= ?", (since,))), Decimal(0))
            if given + AMOUNT > DAILY_CAP:
                return False, "The faucet reached its daily limit. Try again tomorrow."
            (recent,) = con.execute("SELECT COUNT(*) FROM payouts WHERE t >= ?", (now - 60,)).fetchone()
            if recent >= BURST:
                return False, "Too many requests right now. Try again in a minute."
            try:
                txid = rpc("sendtoaddress", address, str(AMOUNT))
            except RPCError as e:
                if "insufficient funds" in str(e).lower():
                    return False, "The faucet is empty for now. The team refills it regularly."
                return False, f"Could not send ({e})."
            con.execute("INSERT INTO payouts VALUES (?, ?, ?, ?, ?)", (now, address, h, str(AMOUNT), txid))
            con.commit()
            return True, txid
        finally:
            con.close()


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PQCH testnet faucet</title>
<style>
:root{{--bg:#f6f7fb;--card:#fff;--ink:#16181d;--muted:#5d6472;--accent:#5b3fd6;--ok:#11743f;--bad:#a8241b;--line:#dfe2ea}}
@media (prefers-color-scheme:dark){{:root{{--bg:#0f1115;--card:#181b22;--ink:#e9ebf0;--muted:#a1a8b5;--accent:#9d88ff;--ok:#5fd394;--bad:#ff8a80;--line:#2a2f3a}}}}
body{{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}}
main{{max-width:560px;margin:0 auto;padding:40px 16px}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:24px}}
h1{{font-size:1.5rem;margin:0 0 4px}} p{{margin:8px 0}} .muted{{color:var(--muted);font-size:.92rem}}
input{{width:100%;box-sizing:border-box;padding:12px;border:1px solid var(--line);border-radius:10px;background:transparent;color:inherit;font:inherit;font-family:ui-monospace,monospace;font-size:.9rem}}
button{{margin-top:12px;width:100%;padding:12px;border:0;border-radius:10px;background:var(--accent);color:#fff;font:inherit;font-weight:600;cursor:pointer}}
.msg{{margin-top:16px;padding:12px;border-radius:10px;border:1px solid var(--line);word-break:break-all}}
.ok{{color:var(--ok)}} .bad{{color:var(--bad)}} code{{font-family:ui-monospace,monospace;font-size:.88rem}}
</style></head><body><main><div class="card">
<h1>PQCH testnet faucet</h1>
<p class="muted">Free coins for testing on the PQCH <strong>testnet</strong>. Test coins have no value.</p>
<form method="post" action="./">
<label for="a">Your testnet address</label>
<input id="a" name="address" placeholder="pqct1p…" autocomplete="off" spellcheck="false" required maxlength="120" value="{address}">
<button type="submit">Send me {amount} test PQCH</button>
</form>
{message}
<p class="muted" style="margin-top:20px">One payout per address and per connection every 24 hours. Coins mined in a block
need 100 blocks before they can be spent; faucet coins can be spent after one confirmation (about a minute).</p>
<p class="muted">Faucet balance: {balance} test PQCH.</p>
</div></main></body></html>
"""


def balance_text():
    try:
        b = rpc("getbalances")["mine"]["trusted"]
        return html.escape(f"{Decimal(b):.2f}")
    except Exception:
        return "unknown"


class Handler(BaseHTTPRequestHandler):
    server_version = "pqch-faucet"
    sys_version = ""

    def client_ip(self):
        if TRUST_PROXY:
            return self.headers.get("X-Real-IP", "") or self.client_address[0]
        return self.client_address[0]

    def send_page(self, status, address="", message=""):
        body = PAGE.format(address=html.escape(address), amount=html.escape(str(AMOUNT)), message=message,
                           balance=balance_text()).encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if urllib.parse.urlsplit(self.path).path not in ("/", "/index.html"):
            self.send_error(404)
            return
        self.send_page(200)

    def do_POST(self):
        if urllib.parse.urlsplit(self.path).path != "/":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY:
            self.send_error(413)
            return
        form = urllib.parse.parse_qs(self.rfile.read(length).decode("utf-8", "replace"))
        address = form.get("address", [""])[0]
        ok, text = request_payout(address, self.client_ip())
        if ok:
            msg = (f'<div class="msg ok">Sent {html.escape(str(AMOUNT))} test PQCH. Transaction:<br>'
                   f'<code>{html.escape(text)}</code></div>')
            self.send_page(200, "", msg)
        else:
            self.send_page(200, address, f'<div class="msg bad">{html.escape(text)}</div>')

    def log_message(self, fmt, *args):
        # No IP addresses in the logs.
        sys.stderr.write("%s %s\n" % (self.log_date_time_string(), fmt % args))


def main():
    if not RPC_PASSWORD:
        sys.exit("RPC_PASSWORD is not set")
    if AMOUNT <= 0 or DAILY_CAP < AMOUNT:
        sys.exit("AMOUNT must be > 0 and DAILY_CAP >= AMOUNT")
    db().close()
    host, port = LISTEN.rsplit(":", 1)
    httpd = ThreadingHTTPServer((host, int(port)), Handler)
    print(f"faucet listening on {LISTEN}, {AMOUNT} per payout", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
