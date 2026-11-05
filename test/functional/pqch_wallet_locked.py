#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: a locked post-quantum wallet cannot generate new addresses (a post-quantum seed has no public
derivation). Payments to addresses past its cached range are missed while it is locked; walletpassphrase
generates them and rescans the blocks seen in the meantime.

Also: importdescriptors never quotes the private descriptor (pqprv) in its errors.
"""

from decimal import Decimal

from test_framework.descriptors import descsum_create
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

KEYPOOL = 3


class PQCHWalletLockedTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False  # post-quantum rules from genesis (P2MR only)
        self.extra_args = [[f"-keypool={KEYPOOL}"], []]
        self.wallet_names = []  # no default wallet with Bitcoin keys (refused on a post-quantum chain)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        payer_node, node = self.nodes[1], self.nodes[0]

        self.log.info("Fund a payer wallet")
        payer_node.createwallet("payer")
        payer = payer_node.get_wallet_rpc("payer")
        self.generatetoaddress(payer_node, 110, payer.getnewaddress())

        self.log.info("Create an encrypted wallet and a copy of its seed that can generate addresses ahead")
        node.createwallet("w")
        w = node.get_wallet_rpc("w")
        w.encryptwallet("pass")
        w.walletpassphrase("pass", 60)
        descs = [d for d in w.listdescriptors(True)["descriptors"] if d["active"]]
        w.walletlock()
        assert all(d["desc"].startswith("p2mr(pqprv(") for d in descs)
        node.createwallet("ahead", blank=True)
        ahead = node.get_wallet_rpc("ahead")
        res = ahead.importdescriptors([{"desc": d["desc"], "timestamp": "now", "active": True,
                                        "internal": d.get("internal", False), "range": [0, 20]} for d in descs])
        assert all(r["success"] for r in res)
        addrs = [ahead.getnewaddress() for _ in range(2 * KEYPOOL)]
        # w (cache 0..KEYPOOL-1) knows the first addresses only
        assert w.getaddressinfo(addrs[KEYPOOL - 1])["ismine"]
        assert not w.getaddressinfo(addrs[KEYPOOL + 1])["ismine"]

        self.log.info("While w is locked: a payment to its last cached address, then one past it")
        payer.sendtoaddress(addrs[KEYPOOL - 1], 1)
        self.generatetoaddress(payer_node, 1, payer.getnewaddress())
        first_height = payer_node.getblockcount()
        payer.sendtoaddress(addrs[KEYPOOL + 1], 2)
        self.generatetoaddress(payer_node, 1, payer.getnewaddress())
        self.sync_all()
        assert_equal(w.getbalances()["mine"]["trusted"], Decimal("1"))
        info = w.getwalletinfo()
        assert_equal(info["rescan_after_unlock_from"], first_height)

        self.log.info("walletpassphrase generates the addresses and rescans: the second payment appears")
        w.walletpassphrase("pass", 60)
        assert_equal(w.getbalances()["mine"]["trusted"], Decimal("3"))
        assert "rescan_after_unlock_from" not in w.getwalletinfo()
        w.walletlock()

        self.log.info("importdescriptors errors never quote the post-quantum seed")
        seed_hex = descs[0]["desc"].split("pqprv(")[1].split(")")[0]
        bad = descsum_create(descs[0]["desc"].split("#")[0].replace("/*h)", "/*)"))  # unhardened step: refused
        node.createwallet("other", blank=True)
        other = node.get_wallet_rpc("other")
        res = other.importdescriptors([{"desc": bad, "timestamp": "now", "active": True}])
        assert not res[0]["success"]
        assert "hardened" in res[0]["error"]["message"]
        assert seed_hex not in str(res[0])
        try:
            other.getdescriptorinfo(bad)
            raise AssertionError("getdescriptorinfo accepted an unhardened p2mr() path")
        except JSONRPCException as e:
            assert seed_hex not in str(e.error)


if __name__ == '__main__':
    PQCHWalletLockedTest(__file__).main()
