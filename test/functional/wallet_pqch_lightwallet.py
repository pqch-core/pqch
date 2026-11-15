#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: the light wallet core (src/pqchwallet, used by the Android app) against the node.

The same 24 recovery words (and passphrase) give the light wallet and PQCH Core the same receive
and change addresses; a transaction the light wallet builds and signs is accepted by the node, with
the fee and size it announced; send-all and subtract-fee work; mistakes are refused with a message.
"""

import ctypes
import json
import os
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, assert_greater_than

ZERO_WORDS = " ".join(["abandon"] * 23 + ["art"])
COIN = 100_000_000


class LightWallet:
    def __init__(self, path):
        self.lib = ctypes.CDLL(path)
        self.lib.pqchw_call.restype = ctypes.c_void_p
        self.lib.pqchw_call.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.pqchw_free.argtypes = [ctypes.c_void_p]

    def raw(self, method, **params):
        ptr = self.lib.pqchw_call(method.encode(), json.dumps(params).encode())
        try:
            return json.loads(ctypes.string_at(ptr).decode())
        finally:
            self.lib.pqchw_free(ptr)

    def call(self, method, **params):
        reply = self.raw(method, **params)
        assert reply["ok"], reply
        return reply["result"]

    def error(self, method, **params):
        reply = self.raw(method, **params)
        assert not reply["ok"], reply
        return reply["error"]


class PQCHLightWalletTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False
        self.extra_args = [["-keypool=30"]]
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        path = os.path.join(self.config["environment"]["BUILDDIR"], "lib", "libpqchwallet.so")
        if not os.path.exists(path):
            raise SkipTest("libpqchwallet.so not built")
        lw = LightWallet(path)
        node = self.nodes[0]

        self.log.info("Before init, and with bad parameters: errors, never a crash")
        assert "init" in lw.error("addresses", mnemonic=ZERO_WORDS, branch=0, **{"from": 0}, count=1)
        assert "unknown method" in lw.error("nope")
        assert "chain" in lw.error("init", chain="signet")
        lw.call("init", chain="regtest")

        self.log.info("New words are 24 valid BIP39 words, different every time")
        words = lw.call("new_mnemonic")["mnemonic"]
        assert_equal(len(words.split()), 24)
        assert words != lw.call("new_mnemonic")["mnemonic"]
        assert lw.call("check_mnemonic", mnemonic=words)["valid"]
        assert lw.call("check_mnemonic", mnemonic="  " + words.upper() + " ")["valid"]
        assert not lw.call("check_mnemonic", mnemonic=" ".join(["abandon"] * 24))["valid"]  # bad checksum
        assert "recovery words" in lw.error("wallet_id", mnemonic="abandon abandon")

        def addresses(branch, count, **kw):
            return [a["address"] for a in lw.call("addresses", mnemonic=words, branch=branch, **{"from": 0}, count=count, **kw)]

        self.log.info("Same words → same receive and change addresses as PQCH Core")
        node.createwallet("core", recovery_words=words)
        core = node.get_wallet_rpc("core")
        receive = addresses(0, 30)
        change = addresses(1, 30)
        assert receive[0].startswith("pqcrt1p")
        assert_equal([core.getnewaddress() for _ in range(5)], receive[:5])
        assert_equal(core.getrawchangeaddress(), change[0])
        assert_equal(lw.call("addresses", mnemonic=words, branch=0, **{"from": 3}, count=1)[0]["address"], receive[3])

        self.log.info("With a passphrase: another wallet, again the same in both")
        node.createwallet("core_pass", recovery_words=words, recovery_passphrase="correct horse")
        core_pass = node.get_wallet_rpc("core_pass")
        with_pass = lw.call("addresses", mnemonic=words, passphrase="correct horse", branch=0, **{"from": 0}, count=1)[0]["address"]
        assert_equal(core_pass.getnewaddress(), with_pass)
        assert with_pass != receive[0]
        assert lw.call("wallet_id", mnemonic=words)["id"] != lw.call("wallet_id", mnemonic=words, passphrase="correct horse")["id"]

        self.log.info("Addresses are checked: P2MR of this network only")
        assert lw.call("check_address", address=receive[0])["valid"]
        assert not lw.call("check_address", address="pqcrt1qnotanaddress")["valid"]
        assert not lw.call("check_address", address="bcrt1p" + "q" * 58)["valid"]

        self.log.info("Fund the light wallet's first two addresses")
        node.createwallet("other")
        other = node.get_wallet_rpc("other")
        self.generatetoaddress(node, 3, receive[0])
        self.generatetoaddress(node, 2, receive[1])
        self.generatetoaddress(node, 101, other.getnewaddress())
        where = {a: (0, i) for i, a in enumerate(receive)}
        where.update({a: (1, i) for i, a in enumerate(change)})

        def utxos():
            # What the app gets from the explorer API, with the branch and index it knows for each address.
            coins = []
            for u in core.listunspent(0):
                branch, index = where[u["address"]]
                coins.append({"txid": u["txid"], "vout": u["vout"], "value": int(u["amount"] * COIN), "branch": branch, "index": index})
            return coins

        coins = utxos()
        assert_equal(len(coins), 5)
        total = sum(c["value"] for c in coins)

        self.log.info("Pay 20 PQCH: accepted by the node, with the fee and size announced")
        to = other.getnewaddress()
        tx = lw.call("create_tx", mnemonic=words, utxos=coins, to=to, amount=20 * COIN, fee_rate=2, change_index=0)
        assert_equal(tx["amount"], 20 * COIN)
        assert_equal(tx["change"]["address"], change[0])
        assert_equal(len(tx["inputs"]), 2)  # 12.5 + 12.5: largest coins first, only as many as needed
        assert_equal(tx["fee"] + tx["amount"] + tx["change"]["value"], sum(c["value"] for c in coins if f'{c["txid"]}:{c["vout"]}' in tx["inputs"]))
        accept = node.testmempoolaccept([tx["hex"]])[0]
        assert accept["allowed"], accept
        assert_equal(accept["vsize"], tx["vsize"])
        assert_equal(int(accept["fees"]["base"] * COIN), tx["fee"])
        assert_greater_than(tx["fee"], 2 * tx["vsize"] - 1)
        assert_equal(node.sendrawtransaction(tx["hex"]), tx["txid"])
        decoded = node.decoderawtransaction(tx["hex"])
        assert all(i["sequence"] == 0xfffffffd for i in decoded["vin"])  # fee can be raised (RBF)
        self.generatetoaddress(node, 1, other.getnewaddress())
        assert_equal(other.getreceivedbyaddress(to), Decimal(20))
        assert_equal(int(core.getbalance() * COIN), total - 20 * COIN - tx["fee"])

        self.log.info("Subtract the fee from the amount: the recipient gets amount - fee")
        coins = utxos()
        to = other.getnewaddress()
        tx = lw.call("create_tx", mnemonic=words, utxos=coins, to=to, amount=5 * COIN, fee_rate=1, change_index=1, subtract_fee=True)
        assert_equal(tx["amount"], 5 * COIN - tx["fee"])
        node.sendrawtransaction(tx["hex"])
        self.generatetoaddress(node, 1, other.getnewaddress())
        assert_equal(int(other.getreceivedbyaddress(to) * COIN), 5 * COIN - tx["fee"])

        self.log.info("Mistakes are refused")
        coins = utxos()
        assert "insufficient funds" in lw.error("create_tx", mnemonic=words, utxos=coins, to=to, amount=10_000 * COIN, fee_rate=1, change_index=2)
        assert "invalid address" in lw.error("create_tx", mnemonic=words, utxos=coins, to="xyz", amount=COIN, fee_rate=1, change_index=2)
        assert "surely a mistake" in lw.error("create_tx", mnemonic=words, utxos=coins, to=to, amount=COIN, fee_rate=50_000, change_index=2)
        assert "twice" in lw.error("create_tx", mnemonic=words, utxos=coins + coins[:1], to=to, amount=COIN, fee_rate=1, change_index=2)
        # Coins of another wallet cannot be signed: the node refuses the transaction.
        wrong = [dict(c, index=c["index"] + 7) for c in coins]
        bad = lw.call("create_tx", mnemonic=words, utxos=wrong, to=to, amount=COIN, fee_rate=1, change_index=2)
        assert not node.testmempoolaccept([bad["hex"]])[0]["allowed"]

        self.log.info("Send everything: no change, the wallet ends empty")
        to = other.getnewaddress()
        tx = lw.call("create_tx", mnemonic=words, utxos=coins, to=to, fee_rate=1, change_index=2, send_all=True)
        assert tx["change"] is None
        assert_equal(len(tx["inputs"]), len(coins))
        assert_equal(tx["amount"] + tx["fee"], sum(c["value"] for c in coins))
        node.sendrawtransaction(tx["hex"])
        self.generatetoaddress(node, 1, other.getnewaddress())
        assert_equal(core.getbalance(), 0)
        assert_equal(int(other.getreceivedbyaddress(to) * COIN), tx["amount"])

        self.log.info("Known BIP39 vector: the all-zero words are valid and give a fixed wallet")
        assert lw.call("check_mnemonic", mnemonic=ZERO_WORDS)["valid"]
        node.createwallet("zero", recovery_words=ZERO_WORDS)
        assert_equal(node.get_wallet_rpc("zero").getnewaddress(),
                     lw.call("addresses", mnemonic=ZERO_WORDS, branch=0, **{"from": 0}, count=1)[0]["address"])


if __name__ == "__main__":
    PQCHLightWalletTest(__file__).main()
