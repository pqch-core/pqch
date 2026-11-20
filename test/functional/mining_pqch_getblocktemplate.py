#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: getblocktemplate and submitblock with the post-quantum rules active.

This is the path a pool (pqch-pool) and any external miner use, and it is not covered by the tests
inherited from Bitcoin Core, which run with the post-quantum rules out of reach. Checks:

- getblocktemplate works (its internal dummy coinbase used to pay OP_TRUE, which the post-quantum
  output rule rejects, so it failed on every network);
- it returns the RandomX key (randomx_seed_hash) and the fees of the mempool transactions;
- a block built from the template with the pool's coinbase layout is accepted by submitblock:
  one P2MR output per miner splitting coinbasevalue, the SegWit commitment, and in the scriptSig the
  BIP34 height, a 32-byte share id and an 8-byte extranonce;
- a coinbase paying a non post-quantum output (OP_TRUE) is rejected (bad-txns-output-not-pq).
"""
from decimal import Decimal

from test_framework.blocktools import NULL_OUTPOINT, add_witness_commitment, create_block, script_BIP34_coinbase_height
from test_framework.messages import COIN, CTransaction, CTxIn, CTxOut, MAX_SEQUENCE_NONFINAL
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

GBT_REQUEST = {"rules": ["segwit"]}


class PQCHGetBlockTemplateTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.legacy_bitcoin_scripts = False  # post-quantum rules active from genesis, as on testnet and mainnet
        self.extra_args = [["-fallbackfee=0.0001"]]
        self.wallet_names = []  # no default wallet with the framework's legacy keys

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def pool_coinbase(self, height, coinbase_value, miner_scripts, share_id, extranonce):
        """Coinbase in the pqch-pool layout: P2MR outputs splitting coinbase_value (rest to the first)."""
        script_sig = CScript(bytes(script_BIP34_coinbase_height(height, padding=False)) + bytes([32]) + share_id + bytes([8]) + extranonce)
        coinbase = CTransaction()
        coinbase.nLockTime = height - 1
        coinbase.vin.append(CTxIn(NULL_OUTPOINT, script_sig, MAX_SEQUENCE_NONFINAL))
        share = coinbase_value // len(miner_scripts)
        for i, spk in enumerate(miner_scripts):
            value = share + (coinbase_value - share * len(miner_scripts) if i == 0 else 0)
            coinbase.vout.append(CTxOut(value, spk))
        return coinbase

    def block_from_template(self, tmpl, coinbase):
        block = create_block(tmpl=tmpl, coinbase=coinbase, txlist=[t["data"] for t in tmpl["transactions"]])
        add_witness_commitment(block)
        block.solve()
        return block

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("w")
        miners = []
        for _ in range(3):
            info = node.getaddressinfo(node.getnewaddress())
            spk = bytes.fromhex(info["scriptPubKey"])
            assert_equal(len(spk), 34)  # P2MR: OP_1 <32 bytes>
            assert_equal(spk[:2], bytes([0x51, 0x20]))
            miners.append((info["address"], CScript(spk)))
        self.generatetoaddress(node, 120, miners[0][0])

        self.log.info("getblocktemplate works with the post-quantum rules active")
        txid = node.sendtoaddress(node.getnewaddress(), 1.5)
        tmpl = node.getblocktemplate(GBT_REQUEST)
        assert_equal(tmpl["height"], 121)
        assert_equal(tmpl["previousblockhash"], node.getbestblockhash())
        assert_equal(len(tmpl["randomx_seed_hash"]), 64)
        assert_equal(tmpl["randomx_seed_height"], 0)  # height 121 <= 4096 + 64: the key is the genesis hash
        assert_equal(tmpl["randomx_seed_hash"], node.getblockhash(0))
        assert_equal([t["txid"] for t in tmpl["transactions"]], [txid])
        fee = tmpl["transactions"][0]["fee"]
        assert_greater_than(fee, 0)
        assert_equal(tmpl["coinbasevalue"], 1250000000 + fee)  # regtest subsidy (12.5 PQCH) + fees
        assert "default_witness_commitment" in tmpl

        self.log.info("A non post-quantum coinbase output is rejected")
        bad = self.pool_coinbase(tmpl["height"], tmpl["coinbasevalue"], [CScript([OP_TRUE])], bytes(32), bytes(8))
        assert_equal(node.submitblock(self.block_from_template(tmpl, bad).serialize().hex()), "bad-txns-output-not-pq")

        self.log.info("A block in the pool's coinbase layout (3 P2MR outputs, share id and extranonce) is accepted")
        share_id = bytes(range(32))
        extranonce = (0x0102030405060708).to_bytes(8, "little")
        cb = self.pool_coinbase(tmpl["height"], tmpl["coinbasevalue"], [spk for _, spk in miners], share_id, extranonce)
        assert len(cb.vin[0].scriptSig) <= 100  # consensus: coinbase scriptSig of 2 to 100 bytes
        block = self.block_from_template(tmpl, cb)
        assert_equal(node.submitblock(block.serialize().hex()), None)
        assert_equal(node.getbestblockhash(), block.hash_hex)
        assert_equal(node.getrawmempool(), [])

        self.log.info("The coinbase pays every miner and carries the share id and the extranonce")
        coinbase = node.getblock(block.hash_hex, 2)["tx"][0]
        paid = [o for o in coinbase["vout"] if o["scriptPubKey"]["type"] != "nulldata"]
        assert_equal([o["scriptPubKey"]["address"] for o in paid], [a for a, _ in miners])
        assert_equal(sum(int(o["value"] * COIN) for o in paid), tmpl["coinbasevalue"])
        assert share_id.hex() + "08" + extranonce.hex() in coinbase["vin"][0]["coinbase"]

        self.log.info("The next template follows the new tip")
        tmpl2 = node.getblocktemplate(GBT_REQUEST)
        assert_equal(tmpl2["previousblockhash"], block.hash_hex)
        assert_equal(tmpl2["coinbasevalue"], 1250000000)
        assert_equal(Decimal(tmpl2["height"]), 122)


if __name__ == '__main__':
    PQCHGetBlockTemplateTest(__file__).main()
