#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: header checkpoints (Consensus::Params::header_checkpoints, -testcheckpoint on regtest).

- A node with checkpoints syncs the same chain, without computing the RandomX proof of work of the
  headers up to the last checkpoint.
- A node whose checkpoint is another chain stops just before the checkpoint height.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

BLOCKS = 1100


class HeaderCheckpointsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.rpc_timeout *= 2

    def setup_network(self):
        self.setup_nodes()  # not connected: node0 mines alone first

    def run_test(self):
        node0 = self.nodes[0]
        self.log.info(f"Mine {BLOCKS} blocks on node0")
        # In batches: each block is a RandomX proof.
        while node0.getblockcount() < BLOCKS:
            self.generate(node0, min(250, BLOCKS - node0.getblockcount()), sync_fun=self.no_op)
        h500 = node0.getblockhash(500)
        h1000 = node0.getblockhash(1000)

        self.log.info("node1 with checkpoints at 500 and 1000 syncs the chain, hashing no header up to 1000")
        self.restart_node(1, extra_args=[f"-testcheckpoint=500:{h500}", f"-testcheckpoint=1000:{h1000}", "-debug=validation"])
        with self.nodes[1].assert_debug_log(["1000 headers up to a checkpoint (height 1000): proof of work not hashed"], timeout=120):
            self.connect_nodes(1, 0)
            self.sync_blocks([node0, self.nodes[1]], timeout=600)
        assert_equal(self.nodes[1].getbestblockhash(), node0.getbestblockhash())

        self.log.info("node2 with a checkpoint for another chain at 500 stops at 499")
        wrong = "11" * 32
        self.restart_node(2, extra_args=[f"-testcheckpoint=500:{wrong}"])
        with self.nodes[2].assert_debug_log(["checkpoint-mismatch"], timeout=120):
            self.connect_nodes(2, 0)
            self.wait_until(lambda: self.nodes[2].getblockchaininfo()["headers"] == 499, timeout=120)
        assert_raises_rpc_error(-8, "Block height out of range", self.nodes[2].getblockhash, 500)

        self.log.info("-testcheckpoint is regtest only and checked")
        self.stop_node(2)
        self.nodes[2].assert_start_raises_init_error([f"-testcheckpoint=500"], expected_msg="Error: -testcheckpoint must be <height>:<block hash>, not '500'.")


if __name__ == '__main__':
    HeaderCheckpointsTest(__file__).main()
