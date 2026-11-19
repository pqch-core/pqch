#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: the ML-DSA emergency soft fork (deployment "mldsaemergency", contrib/pqch/SEGURANCA.md).

- Nodes never signal it on their own: blocks carry the bit only with -signalmldsaemergency.
- With 90% of a period signalling it locks in, and one period later it is active.
- Once active, a spend through the hybrid leaf (Schnorr + ML-DSA) is invalid, in the mempool and in
  a block, even when it was signed before activation.
- The wallet then spends through the SLH-DSA recovery leaf by itself (send, sendall).
- A node that does not signal enforces the rule too.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

DEPLOYMENT = "mldsaemergency"
BIT = 1
PERIOD = 144
SLHDSA128S_SIG_SIZE = 7856
MLDSA44_SIG_SIZE = 2420


class PQCHMLDSAEmergencyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False  # post-quantum rules from genesis (P2MR only)
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def status(self, node):
        return node.getdeploymentinfo()["deployments"][DEPLOYMENT]["bip9"]["status"]

    def witness_sizes(self, node, txid):
        tx = node.getrawtransaction(txid, True)
        return [(len(vin["txinwitness"]), len(bytes.fromhex(vin["txinwitness"][0]))) for vin in tx["vin"]]

    def run_test(self):
        node, other = self.nodes
        node.createwallet("w")
        w = node.get_wallet_rpc("w")
        self.generatetoaddress(node, PERIOD + 10, w.getnewaddress())

        self.log.info("Started, but nobody signals by default")
        assert_equal(self.status(node), "started")
        tip = node.getblockheader(node.getbestblockhash())
        assert_equal((tip["version"] >> BIT) & 1, 0)

        self.log.info("A hybrid-leaf spend signed before activation")
        dest = w.getnewaddress()
        pre = w.send({dest: 1}, options={"add_to_wallet": False})
        pre_hex = pre["hex"]
        pre_txid = node.decoderawtransaction(pre_hex)["txid"]
        wit = node.decoderawtransaction(pre_hex)["vin"][0]["txinwitness"]
        assert_equal((len(wit), len(bytes.fromhex(wit[0]))), (4, MLDSA44_SIG_SIZE))
        assert node.testmempoolaccept([pre_hex])[0]["allowed"]

        self.log.info("Miners choose to signal: -signalmldsaemergency")
        self.restart_node(0, extra_args=["-signalmldsaemergency=1"])
        self.connect_nodes(0, 1)
        node.loadwallet("w")
        w = node.get_wallet_rpc("w")
        for _ in range(5):
            if self.status(node) == "active":
                break
            self.generatetoaddress(node, PERIOD, w.getnewaddress())
        tip = node.getblockheader(node.getbestblockhash())
        assert_equal(self.status(node), "active")
        self.log.info(f"Active at height {node.getdeploymentinfo()['deployments'][DEPLOYMENT]['height']}")
        self.sync_blocks()
        assert_equal(self.status(other), "active")

        self.log.info("The pre-activation hybrid spend is now invalid, on both nodes")
        for n in (node, other):
            res = n.testmempoolaccept([pre_hex])[0]
            assert not res["allowed"]
            assert "ML-DSA signatures are disabled" in res["reject-reason"] or "mandatory-script-verify-flag" in res["reject-reason"], res
        assert_raises_rpc_error(-25, None, self.generateblock, node, w.getnewaddress(), [pre_hex], sync_fun=self.no_op)
        assert pre_txid not in node.getrawmempool()

        self.log.info("The wallet now spends through the SLH-DSA recovery leaf by itself")
        sent = w.send({dest: 1})
        assert sent["complete"]
        assert all(s == (3, SLHDSA128S_SIG_SIZE) for s in self.witness_sizes(node, sent["txid"]))
        txid = w.sendtoaddress(w.getnewaddress(), 2)
        assert all(s == (3, SLHDSA128S_SIG_SIZE) for s in self.witness_sizes(node, txid))
        self.generatetoaddress(node, 1, w.getnewaddress())
        assert_equal(w.gettransaction(sent["txid"])["confirmations"], 1)

        self.log.info("sendall works too, and a node that does not signal follows the new rule")
        other.createwallet("o")
        to_other = other.get_wallet_rpc("o").getnewaddress()
        node.createwallet("s")
        small = node.get_wallet_rpc("s")
        w.send({small.getnewaddress(): 3})
        self.generatetoaddress(node, 1, w.getnewaddress())
        res = small.sendall([to_other])
        assert all(s == (3, SLHDSA128S_SIG_SIZE) for s in self.witness_sizes(node, res["txid"]))
        self.generatetoaddress(node, 1, w.getnewaddress())
        assert_equal(other.get_wallet_rpc("o").getbalances()["mine"]["trusted"] > 0, True)

if __name__ == '__main__':
    PQCHMLDSAEmergencyTest(__file__).main()
