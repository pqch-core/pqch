#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: 24 BIP39 recovery words.

A new wallet's post-quantum seed comes from 24 new BIP39 words, kept in the wallet (encrypted if it
is) and shown by getrecoverywords. createwallet recovery_words restores a wallet from them, with the
same addresses and, after the rescan, the same balance. The optional BIP39 passphrase gives a
different wallet. The words survive a restart; encryptwallet rotates to new words, as it rotates keys.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# BIP39 vector: 32 zero bytes of entropy
ZERO_WORDS = " ".join(["abandon"] * 23 + ["art"])


class PQCHWalletRecoveryWordsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False  # post-quantum rules from genesis (P2MR only)
        # Restoring looks this many addresses ahead (the real default is 1000; the test framework uses 1).
        self.extra_args = [["-keypool=20"]]
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("A new wallet has 24 valid recovery words")
        node.createwallet("w1")
        w1 = node.get_wallet_rpc("w1")
        words = w1.getrecoverywords()["words"]
        assert_equal(len(words.split()), 24)
        first_addr = w1.getnewaddress()
        assert first_addr.startswith("pqcrt1p")

        self.log.info("Fund it, receive a payment on a later address, spend from it")
        self.generatetoaddress(node, 110, first_addr)
        later_addr = [w1.getnewaddress() for _ in range(5)][-1]
        node.createwallet("other")
        other = node.get_wallet_rpc("other")
        txid = w1.sendtoaddress(other.getnewaddress(), 3)
        self.generatetoaddress(node, 1, other.getnewaddress())
        w1.sendtoaddress(later_addr, 2)
        self.generatetoaddress(node, 1, other.getnewaddress())
        balance = w1.getbalance()
        assert balance > 0

        self.log.info("Restore from the words: same addresses, same balance after the rescan")
        node.createwallet("w1_restored", recovery_words=words)
        r = node.get_wallet_rpc("w1_restored")
        assert_equal(r.getrecoverywords()["words"], words)
        assert_equal(r.getbalance(), balance)
        assert_equal(r.getaddressinfo(first_addr)["ismine"], True)
        assert_equal(r.getaddressinfo(later_addr)["ismine"], True)
        assert_equal(r.gettransaction(txid)["amount"], Decimal("-3"))
        assert_equal(r.getnewaddress(), w1.getnewaddress())
        assert_equal([d["desc"] for d in r.listdescriptors()["descriptors"]],
                     [d["desc"] for d in w1.listdescriptors()["descriptors"]])
        # and it can spend
        r.sendtoaddress(other.getnewaddress(), 1)

        self.log.info("Upper case and extra spaces are accepted; the stored words are normalized")
        node.createwallet("w1_messy", recovery_words="  " + words.upper().replace(" ", "   ") + " ")
        assert_equal(node.get_wallet_rpc("w1_messy").getrecoverywords()["words"], words)

        self.log.info("The BIP39 passphrase gives a different (empty) wallet")
        node.createwallet("w1_pass", recovery_words=words, recovery_passphrase="extra")
        p = node.get_wallet_rpc("w1_pass")
        assert_equal(p.getbalance(), 0)
        assert_equal(p.getaddressinfo(first_addr)["ismine"], False)
        assert_equal(p.getrecoverywords()["words"], words)  # the words; the passphrase is not stored
        node.createwallet("w1_pass_again", recovery_words=words, recovery_passphrase="extra")
        assert_equal(node.get_wallet_rpc("w1_pass_again").getnewaddress(), p.getnewaddress())

        self.log.info("Standard BIP39 words are accepted (zero-entropy vector)")
        node.createwallet("zero", recovery_words=ZERO_WORDS)
        assert_equal(node.get_wallet_rpc("zero").getrecoverywords()["words"], ZERO_WORDS)

        self.log.info("Invalid words are refused, and no wallet is left behind")
        bad_checksum = " ".join(["abandon"] * 24)
        assert_raises_rpc_error(-4, "Invalid recovery words", node.createwallet, "bad1", recovery_words=bad_checksum)
        assert_raises_rpc_error(-4, "Invalid recovery words", node.createwallet, "bad2", recovery_words=" ".join(words.split()[:12]))
        assert_raises_rpc_error(-4, "Invalid recovery words", node.createwallet, "bad3", recovery_words=words.replace(words.split()[0], "bitcoinz", 1))
        assert "bad1" not in node.listwalletdir()["wallets"]
        assert_raises_rpc_error(-8, "recovery_passphrase requires recovery_words", node.createwallet, "bad4", recovery_passphrase="x")
        assert_raises_rpc_error(-4, "cannot be used with", node.createwallet, "bad5", disable_private_keys=True, recovery_words=words)
        assert_raises_rpc_error(-4, "cannot be used with", node.createwallet, "bad6", blank=True, recovery_words=words)

        self.log.info("Encrypted wallets: words only when unlocked; restore into an encrypted wallet")
        node.createwallet("e1", passphrase="pw")
        e1 = node.get_wallet_rpc("e1")
        assert_raises_rpc_error(-13, "walletpassphrase", e1.getrecoverywords)
        e1.walletpassphrase("pw", 30)
        e_words = e1.getrecoverywords()["words"]
        e_addr = e1.getnewaddress()
        e1.walletlock()
        node.createwallet("e1_restored", passphrase="pw2", recovery_words=e_words)
        er = node.get_wallet_rpc("e1_restored")
        assert_raises_rpc_error(-13, "walletpassphrase", er.getrecoverywords)
        er.walletpassphrase("pw2", 30)
        assert_equal(er.getrecoverywords()["words"], e_words)
        assert_equal(er.getaddressinfo(e_addr)["ismine"], True)
        er.walletlock()

        self.log.info("The words survive a restart")
        self.restart_node(0)
        for name in ["w1", "w1_restored", "e1"]:
            node.loadwallet(name)
        assert_equal(node.get_wallet_rpc("w1").getrecoverywords()["words"], words)
        e1 = node.get_wallet_rpc("e1")
        e1.walletpassphrase("pw", 30)
        assert_equal(e1.getrecoverywords()["words"], e_words)
        e1.walletlock()

        self.log.info("encryptwallet rotates to new words (as it rotates keys); old funds stay")
        w1 = node.get_wallet_rpc("w1")
        before = w1.getbalance()
        w1.encryptwallet("later")
        w1.walletpassphrase("later", 30)
        new_words = w1.getrecoverywords()["words"]
        assert new_words != words
        assert_equal(len(new_words.split()), 24)
        assert_equal(w1.getbalance(), before)
        assert_equal(w1.getaddressinfo(first_addr)["ismine"], True)

        self.log.info("Wallets whose keys were imported have no words")
        node.createwallet("imported", blank=True)
        imp = node.get_wallet_rpc("imported")
        descs = [d for d in node.get_wallet_rpc("w1_restored").listdescriptors(True)["descriptors"]]
        assert all(imp.importdescriptors([{"desc": d["desc"], "timestamp": 0, "active": d["active"],
                                            "internal": d.get("internal", False)}])[0]["success"] for d in descs)
        assert_raises_rpc_error(-4, "no recovery words", imp.getrecoverywords)


if __name__ == '__main__':
    PQCHWalletRecoveryWordsTest(__file__).main()
