#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: spending standard P2MR outputs through the recovery leaf (SLH-DSA-128s) from the wallet RPCs
(option p2mr_recovery of send, sendall and walletcreatefundedpsbt, and argument of walletprocesspsbt).

The recovery witness has 3 elements (signature, leaf, control block) instead of 4 and is about twice
the size; the wallet must size the fee for it.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than_or_equal

SLHDSA128S_SIG_SIZE = 7856
MLDSA44_SIG_SIZE = 2420
FEE_RATE = 10  # sat/vB


class PQCHWalletRecoveryTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False  # post-quantum rules from genesis (P2MR only)
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def check_spend(self, txid, recovery):
        node = self.nodes[0]
        tx = node.getrawtransaction(txid, True)
        for vin in tx["vin"]:
            wit = vin["txinwitness"]
            if recovery:
                assert_equal(len(wit), 3)
                assert_equal(len(bytes.fromhex(wit[0])), SLHDSA128S_SIG_SIZE)
            else:
                assert_equal(len(wit), 4)
                assert_equal(len(bytes.fromhex(wit[0])), MLDSA44_SIG_SIZE)
        entry = node.getmempoolentry(txid)
        # the fee covers the requested rate for the real size (no underestimate)
        assert_greater_than_or_equal(entry["fees"]["base"] * Decimal(1e8), FEE_RATE * entry["vsize"])
        return entry["vsize"]

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("w")
        w = node.get_wallet_rpc("w")
        self.generatetoaddress(node, 105, w.getnewaddress())
        dest = w.getnewaddress()

        self.log.info("RPCs name the P2MR output type p2mr")
        spk = w.getaddressinfo(dest)["scriptPubKey"]
        assert_equal(node.decodescript(spk)["type"], "p2mr")
        coinbase = node.getblock(node.getbestblockhash(), 2)["tx"][0]
        assert_equal(coinbase["vout"][0]["scriptPubKey"]["type"], "p2mr")
        # Without spending data a witness v1 program is just an address: never a Taproot rawtr(),
        # even when it is a valid x-only key (Bitcoin shows this one as rawtr(ee…)).
        other = node.decodescript("5120" + "ee" * 32)
        assert_equal(other["type"], "p2mr")
        assert other["desc"].startswith("addr(")

        self.log.info("send: hybrid leaf by default, recovery leaf with p2mr_recovery")
        hybrid = w.send({dest: 1}, fee_rate=FEE_RATE)
        vsize_hybrid = self.check_spend(hybrid["txid"], recovery=False)
        rec = w.send({dest: 1}, fee_rate=FEE_RATE, options={"p2mr_recovery": True})
        vsize_rec = self.check_spend(rec["txid"], recovery=True)
        assert_greater_than_or_equal(vsize_rec, vsize_hybrid + 1000)
        block = self.generatetoaddress(node, 1, w.getnewaddress())[0]
        # getrawtransaction with prevouts (verbosity 2) and decoderawtransaction name P2MR too
        tx = node.getrawtransaction(hybrid["txid"], 2, block)
        assert all(vin["prevout"]["scriptPubKey"]["type"] == "p2mr" for vin in tx["vin"])
        assert all(vout["scriptPubKey"]["type"] == "p2mr" for vout in tx["vout"])
        assert all(vout["scriptPubKey"]["type"] == "p2mr" for vout in node.decoderawtransaction(tx["hex"])["vout"])

        self.log.info("walletcreatefundedpsbt + walletprocesspsbt with p2mr_recovery")
        psbt = w.walletcreatefundedpsbt([], [{dest: 1}], 0, {"fee_rate": FEE_RATE, "p2mr_recovery": True})["psbt"]
        res = w.walletprocesspsbt(psbt, True, "DEFAULT", True, True, True)
        assert res["complete"]
        txid = node.sendrawtransaction(res["hex"])
        self.check_spend(txid, recovery=True)
        self.generatetoaddress(node, 1, w.getnewaddress())

        self.log.info("sendall with p2mr_recovery")
        node.createwallet("other")
        other = node.get_wallet_rpc("other")
        res = w.sendall([other.getnewaddress()], fee_rate=FEE_RATE, options={"p2mr_recovery": True})
        self.check_spend(res["txid"], recovery=True)


if __name__ == '__main__':
    PQCHWalletRecoveryTest(__file__).main()
