#!/usr/bin/env python3
# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""PQCH: agent vaults (contrib/pqch/COFRE.md).

The owner's wallet gives a recovery key (getvaultkey), the agent's wallet makes the vault with it
(createvault) and the owner imports the vault (importdescriptors). The agent spends through the hot
leaf as usual; the owner cannot spend the vault's coins that way, and recovers them through the
recovery leaf (recovervault) once they have `older` confirmations, never before.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

SLHDSA128S_SIG_SIZE = 7856
MLDSA44_SIG_SIZE = 2420
FEE_RATE = 10  # sat/vB
OLDER = 10


class PQCHWalletVaultTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # As on a quiet testnet node: no fee estimate and no -fallbackfee (recovervault must still work).
        self.extra_args = [["-fallbackfee=0"]]
        self.legacy_bitcoin_scripts = False  # post-quantum rules from genesis (P2MR only)
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        for name in ("funder", "owner", "agent"):
            node.createwallet(name)
        funder = node.get_wallet_rpc("funder")
        owner = node.get_wallet_rpc("owner")
        agent = node.get_wallet_rpc("agent")
        self.generatetoaddress(node, 110, funder.getnewaddress())

        self.log.info("The owner gives a recovery key; the agent makes the vault with it")
        key = owner.getvaultkey()["key"]
        # Found on the testnet (0.2.6): with its own key a wallet made a vault it could only recover.
        assert_raises_rpc_error(-8, "this wallet's own recovery key", owner.createvault, key, OLDER)
        assert_equal(owner.listvaults(), [])
        assert key.startswith("slh([") and key.endswith(")")
        assert_equal(owner.getvaultkey(0)["key"], key)
        assert owner.getvaultkey(1)["key"] != key
        assert_raises_rpc_error(-8, "owner_key must be", agent.createvault, "slh(00)," + key, OLDER)
        assert_raises_rpc_error(-8, "older must be", agent.createvault, key, 65536)
        vault = agent.createvault(key, OLDER)
        assert_equal(len(vault["descriptors"]), 2)
        for desc in vault["descriptors"]:
            assert desc.startswith("p2mr_vault(pqseed(") and f"older({OLDER})" in desc and "pqprv(" not in desc
        for item in vault["owner_import"]:
            assert item["desc"].startswith("p2mr_vault(pqprv(")

        self.log.info("The agent's recovery words still work with the vault as active descriptor")
        words = agent.getrecoverywords()["words"]
        assert agent.verifyrecoverywords(words)["valid"]

        self.log.info("The owner imports the vault")
        res = owner.importdescriptors(vault["owner_import"])
        assert all(r["success"] for r in res), res
        roles = {(v["role"], v["internal"]) for v in owner.listvaults()}
        assert_equal(roles, {("owner", False), ("owner", True)})
        assert_equal({(v["role"], v["internal"], v["active"]) for v in agent.listvaults()}, {("agent", False, True), ("agent", True, True)})

        self.log.info("The agent's new addresses and change are vault addresses the owner also sees")
        addr = agent.getnewaddress()
        change = agent.getrawchangeaddress()
        for a in (addr, change):
            assert agent.getaddressinfo(a)["ismine"]
            assert owner.getaddressinfo(a)["ismine"]
        funder.send({addr: 5}, fee_rate=FEE_RATE)

        self.log.info("A coin not yet confirmed shows in listvaults (both sides) and waits in recovervault")
        for wallet, role in ((owner, "owner"), (agent, "agent")):
            [v] = [v for v in wallet.listvaults() if not v["internal"]]
            assert_equal((v["role"], v["amount"], v["unconfirmed"], v["recoverable"]), (role, 5, 5, 0))
        assert_raises_rpc_error(-6, "No vault coin can be recovered yet (1 waiting", owner.recovervault)
        self.generatetoaddress(node, 1, funder.getnewaddress())
        [v] = [v for v in owner.listvaults() if not v["internal"]]
        assert_equal((v["amount"], v["unconfirmed"]), (5, 0))

        self.log.info("The agent spends through the hot leaf; the change stays in the vault")
        pay = funder.getnewaddress()
        txid = agent.send({pay: 1}, fee_rate=FEE_RATE)["txid"]
        tx = node.getrawtransaction(txid, True)
        assert_equal(len(tx["vin"][0]["txinwitness"]), 4)
        assert_equal(len(bytes.fromhex(tx["vin"][0]["txinwitness"][0])), MLDSA44_SIG_SIZE)
        change_out = [o for o in tx["vout"] if o["scriptPubKey"]["address"] != pay]
        assert_equal(len(change_out), 1)
        ca = change_out[0]["scriptPubKey"]["address"]
        assert owner.getaddressinfo(ca)["ismine"]
        self.generatetoaddress(node, 1, funder.getnewaddress())

        self.log.info("After a restart, both wallets load with their vaults and roles")
        self.restart_node(0)
        for name in ("funder", "owner", "agent"):
            node.loadwallet(name)
        funder, owner, agent = (node.get_wallet_rpc(n) for n in ("funder", "owner", "agent"))
        assert_equal({v["role"] for v in owner.listvaults()}, {"owner"})
        assert_equal({(v["role"], v["active"]) for v in agent.listvaults()}, {("agent", True)})
        assert agent.getaddressinfo(agent.getnewaddress())["parent_desc"].startswith("p2mr_vault(")

        self.log.info("The owner cannot spend the vault through the hot leaf, nor count it in its balance")
        assert_equal(owner.getbalance(), 0)
        assert_equal(owner.getbalances()["mine"]["untrusted_pending"], 0)
        assert_raises_rpc_error(-6, "Insufficient funds", owner.sendtoaddress, pay, 1, fee_rate=FEE_RATE)

        self.log.info("Before `older` confirmations, nothing can be recovered")
        [v] = [v for v in owner.listvaults() if v["internal"]]
        assert_equal(v["recoverable"], 0)
        assert_equal(v["next_recoverable_in"], OLDER - 1)
        assert_raises_rpc_error(-6, "No vault coin can be recovered yet", owner.recovervault)
        # listunspent leaves out the owner's vault coins (only recovervault spends them)
        assert_equal([u for u in owner.listunspent(0) if u["txid"] == txid], [])
        coin = {"vout": change_out[0]["n"]}
        dest = owner.getnewaddress()
        # The signer refuses a recovery spend whose nSequence is below the timelock (CSV would fail).
        psbt = owner.walletcreatefundedpsbt([{"txid": txid, "vout": coin["vout"], "sequence": OLDER - 1}], [{dest: 1}], 0,
                                            {"add_inputs": False, "fee_rate": FEE_RATE, "p2mr_recovery": True})["psbt"]
        assert not owner.walletprocesspsbt(psbt, True, "DEFAULT", True, True, True)["complete"]
        # With the right nSequence the coin is still too young for the mempool (BIP68).
        psbt = owner.walletcreatefundedpsbt([{"txid": txid, "vout": coin["vout"], "sequence": OLDER}], [{dest: 1}], 0,
                                            {"add_inputs": False, "fee_rate": FEE_RATE, "p2mr_recovery": True})["psbt"]
        signed = owner.walletprocesspsbt(psbt, True, "DEFAULT", True, True, True)
        assert signed["complete"]
        assert_raises_rpc_error(-26, "non-BIP68-final", node.sendrawtransaction, signed["hex"])

        self.log.info("After `older` confirmations the owner recovers through the recovery leaf")
        self.generatetoaddress(node, OLDER - 2, funder.getnewaddress())
        assert_raises_rpc_error(-6, "No vault coin can be recovered yet", owner.recovervault)
        self.generatetoaddress(node, 1, funder.getnewaddress())
        total = sum(v["amount"] for v in owner.listvaults())
        assert_equal(sum(v["recoverable"] for v in owner.listvaults()), total)
        res = owner.recovervault(dest, FEE_RATE)
        assert_equal(res["inputs"], 1)
        assert_equal(res["waiting"], [])
        assert_equal(res["amount"] + res["fee"], total)
        assert_equal(res["fee_source"], "given")
        assert FEE_RATE <= res["fee_rate"] < FEE_RATE + 1
        tx = node.getrawtransaction(res["txid"], True)
        wit = tx["vin"][0]["txinwitness"]
        assert_equal(len(wit), 3)
        assert_equal(len(bytes.fromhex(wit[0])), SLHDSA128S_SIG_SIZE)
        assert "OP_CHECKSEQUENCEVERIFY" in node.decodescript(wit[1])["asm"]
        assert_equal(tx["vin"][0]["sequence"], OLDER)
        entry = node.getmempoolentry(res["txid"])
        assert entry["fees"]["base"] * Decimal(1e8) >= FEE_RATE * entry["vsize"]
        self.generatetoaddress(node, 1, funder.getnewaddress())
        assert_equal(sum(v["amount"] for v in owner.listvaults()), 0)
        assert_equal(owner.getbalance(), res["amount"])
        assert_raises_rpc_error(-6, "owns no vault coins", owner.recovervault)

        self.log.info("older(0): the owner can recover at once (emergency button)")
        key1 = owner.getvaultkey(1)["key"]
        vault0 = agent.createvault(key1, 0, 1)
        # The owner makes its copy of this vault its active receiving descriptor: then its new
        # addresses are vault addresses, and a recovery must still never go back into the vault.
        owner_import = [dict(item, active=True) for item in vault0["owner_import"]]
        assert all(r["success"] for r in owner.importdescriptors(owner_import))
        assert owner.getaddressinfo(owner.getnewaddress())["parent_desc"].startswith("p2mr_vault(")
        vaults = agent.listvaults()
        assert_equal(len(vaults), 4)
        assert_equal({v["older"] for v in vaults if v["active"]}, {0})
        addr0 = agent.getnewaddress()
        funder.send({addr0: 2}, fee_rate=FEE_RATE)
        self.generatetoaddress(node, 1, funder.getnewaddress())
        self.log.info("Without fee_rate and without a fee estimate, the recovery uses the default rate")
        assert_raises_rpc_error(-6, "Fee estimation failed", owner.sendtoaddress, pay, 1)  # what a plain send does
        assert_raises_rpc_error(-5, "back into the vault", owner.recovervault, owner.getnewaddress())
        res = owner.recovervault()  # no address: a new one of the owner's own p2mr() descriptor
        assert_equal(res["inputs"], 1)
        [out] = node.getrawtransaction(res["txid"], True)["vout"]
        info = owner.getaddressinfo(out["scriptPubKey"]["address"])
        assert info["ismine"] and info["parent_desc"].startswith("p2mr("), info
        assert_equal(res["fee_source"], "default")
        assert 10 <= res["fee_rate"] < 11
        tx = node.getrawtransaction(res["txid"], True)
        assert "OP_CHECKSEQUENCEVERIFY" not in node.decodescript(tx["vin"][0]["txinwitness"][1])["asm"]

        self.log.info("The agent's recovery words restore the same vault")
        node.createwallet(wallet_name="agent2", recovery_words=words)
        agent2 = node.get_wallet_rpc("agent2")
        assert_equal(agent2.createvault(key, OLDER)["descriptors"], vault["descriptors"])


if __name__ == '__main__':
    PQCHWalletVaultTest(__file__).main()
