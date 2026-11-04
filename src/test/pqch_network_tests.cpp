// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <node/miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <pow_randomx.h>
#include <script/interpreter.h>
#include <test/util/pqch.h>
#include <test/util/setup_common.h>
#include <test/util/validation.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <vector>

/**
 * PQCH phase 4: network rules (whitepaper, sections 11, 15, 16 and 21.3).
 */
BOOST_FIXTURE_TEST_SUITE(pqch_network_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(network_rule_constants)
{
    const auto main{CreateChainParams(*m_node.args, ChainType::MAIN)};
    const auto& consensus{main->GetConsensus()};
    BOOST_CHECK_EQUAL(consensus.nCoinbaseMaturity, 720);
    BOOST_CHECK_EQUAL(main->MaxReorgDepth(), 60);
    BOOST_CHECK_EQUAL(consensus.PQRulesHeight, 0);
    BOOST_CHECK_EQUAL(consensus.DeploymentHeight(Consensus::DEPLOYMENT_PQRULES), 0);
    BOOST_CHECK_EQUAL(MAX_FUTURE_BLOCK_TIME, 600);
    BOOST_CHECK_EQUAL(MAX_BLOCK_WEIGHT, 2'000'000U);
    BOOST_CHECK(DEFAULT_BLOCK_MAX_WEIGHT <= MAX_BLOCK_WEIGHT);
    BOOST_CHECK(MAX_STANDARD_TX_WEIGHT <= MAX_BLOCK_WEIGHT);

    const auto regtest{CreateChainParams(*m_node.args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(regtest->GetConsensus().nCoinbaseMaturity, COINBASE_MATURITY);
    BOOST_CHECK_EQUAL(regtest->MaxReorgDepth(), 60);
}

BOOST_AUTO_TEST_CASE(coinbase_maturity_720)
{
    CCoinsViewCache view{&CoinsViewEmpty::Get()};
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const int coinbase_height{1000};
    view.AddCoin(prevout, Coin{CTxOut{12 * COIN, pqch_test::MakeTree({CScript() << OP_TRUE}).ScriptPubKey()}, coinbase_height, /*fCoinBase=*/true}, false);

    CMutableTransaction spend;
    spend.vin.emplace_back(prevout);
    spend.vout.emplace_back(11 * COIN, CScript() << OP_RETURN);
    const CTransaction tx{spend};

    for (int depth : {100, 719, 720, 721}) {
        TxValidationState state;
        CAmount fee;
        const bool ok{Consensus::CheckTxInputs(tx, state, view, coinbase_height + depth, fee, MAINNET_COINBASE_MATURITY)};
        BOOST_CHECK_EQUAL(ok, depth >= 720);
        if (!ok) BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-premature-spend-of-coinbase");
    }
}

namespace {

/** Builds and mines blocks on any parent, for competing branches. */
struct BranchMiner {
    ChainstateManager& chainman;

    CBlock Make(const CBlockIndex* prev, const CScript& coinbase_script, uint8_t branch_tag) const
    {
        CBlock block;
        block.nVersion = 0x20000000;
        block.hashPrevBlock = prev->GetBlockHash();
        block.nTime = prev->GetBlockTime() + 1; // stays within MAX_FUTURE_BLOCK_TIME of the mock clock
        block.nBits = GetNextWorkRequired(prev, &block, chainman.GetConsensus());
        CMutableTransaction coinbase;
        coinbase.vin.emplace_back(COutPoint{});
        coinbase.vin[0].scriptSig = CScript() << (prev->nHeight + 1) << std::vector<unsigned char>{branch_tag};
        coinbase.vout.emplace_back(GetBlockSubsidy(prev->nHeight + 1, chainman.GetConsensus()), coinbase_script);
        block.vtx.push_back(MakeTransactionRef(coinbase));
        block.hashMerkleRoot = BlockMerkleRoot(block);
        while (!CheckBlockProofOfWork(block, prev, chainman.GetConsensus())) ++block.nNonce;
        return block;
    }

    /** Extend `from` with `count` blocks; returns the last one's index. */
    const CBlockIndex* Extend(const CBlockIndex* from, int count, const CScript& coinbase_script, uint8_t branch_tag) const
    {
        const CBlockIndex* tip{from};
        for (int i = 0; i < count; ++i) {
            auto block{std::make_shared<const CBlock>(Make(tip, coinbase_script, branch_tag))};
            bool new_block{false};
            BOOST_REQUIRE(chainman.ProcessNewBlock(block, /*force_processing=*/true, /*min_pow_checked=*/true, &new_block));
            tip = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block->GetHash()));
            BOOST_REQUIRE(tip);
        }
        return tip;
    }
};

const CBlockIndex* Tip(ChainstateManager& chainman)
{
    return WITH_LOCK(::cs_main, return chainman.ActiveChain().Tip());
}

} // namespace

BOOST_FIXTURE_TEST_CASE(reorg_depth_limit, TestChain100Setup)
{
    auto& chainman{static_cast<TestChainstateManager&>(*m_node.chainman)};
    if (chainman.IsInitialBlockDownload()) chainman.JumpOutOfIbd();
    BOOST_REQUIRE(!chainman.IsInitialBlockDownload());
    BOOST_REQUIRE_EQUAL(chainman.MaxReorgDepth(), 60);
    const BranchMiner miner{chainman};

    // 60 blocks deep: allowed. Local branch of 60 blocks, competing branch of 61 from the same fork point.
    const CBlockIndex* fork{Tip(chainman)};
    const CBlockIndex* local{miner.Extend(fork, 60, m_coinbase_script, 1)};
    BOOST_CHECK_EQUAL(Tip(chainman), local);
    const CBlockIndex* other{miner.Extend(fork, 61, m_coinbase_script, 2)};
    BOOST_CHECK_EQUAL(Tip(chainman), other);

    // 61 blocks deep: refused, even though the competing branch has more work.
    fork = Tip(chainman);
    local = miner.Extend(fork, 61, m_coinbase_script, 3);
    BOOST_CHECK_EQUAL(Tip(chainman), local);
    other = miner.Extend(fork, 63, m_coinbase_script, 4);
    BOOST_CHECK(other->nChainWork > local->nChainWork);
    BOOST_CHECK_EQUAL(Tip(chainman), local);
    // The refused blocks are kept and not marked invalid.
    BOOST_CHECK(!(WITH_LOCK(::cs_main, return other->nStatus) & BLOCK_FAILED_VALID));

    // Manual reconciliation (whitepaper, section 16): the operator invalidates the first block of
    // the local branch, and the node follows the other chain.
    const CBlockIndex* first_local{WITH_LOCK(::cs_main, return local->GetAncestor(fork->nHeight + 1))};
    BlockValidationState state;
    BOOST_REQUIRE(chainman.ActiveChainstate().InvalidateBlock(state, const_cast<CBlockIndex*>(first_local)));
    BOOST_REQUIRE(chainman.ActiveChainstate().ActivateBestChain(state));
    BOOST_CHECK_EQUAL(Tip(chainman), other);
}

BOOST_FIXTURE_TEST_CASE(future_block_time_600s, TestChain100Setup)
{
    auto& chainman{*m_node.chainman};
    const BranchMiner miner{chainman};
    const CBlockIndex* tip{Tip(chainman)};
    const int64_t now{TicksSinceEpoch<std::chrono::seconds>(NodeClock::now())};

    for (int64_t ahead : {int64_t{600}, int64_t{601}}) {
        CBlock block{miner.Make(tip, m_coinbase_script, uint8_t(ahead & 0xff))};
        block.nTime = now + ahead;
        block.nBits = GetNextWorkRequired(tip, &block, chainman.GetConsensus());
        while (!CheckBlockProofOfWork(block, tip, chainman.GetConsensus())) ++block.nNonce;
        BlockValidationState state;
        const CBlockIndex* index{nullptr};
        const bool ok{chainman.ProcessNewBlockHeaders(std::vector<CBlockHeader>{block}, /*min_pow_checked=*/true, state, &index)};
        BOOST_CHECK_EQUAL(ok, ahead <= 600);
        if (!ok) BOOST_CHECK_EQUAL(state.GetRejectReason(), "time-too-new");
    }
}

namespace {
struct PQChainSetup : public TestChain100Setup {
    PQChainSetup() : TestChain100Setup{ChainType::REGTEST, {.legacy_bitcoin_scripts = false}} {}
};
} // namespace

BOOST_FIXTURE_TEST_CASE(pq_rules_in_blocks_and_mempool, PQChainSetup)
{
    auto& chainman{*m_node.chainman};
    BOOST_REQUIRE_EQUAL(chainman.GetConsensus().PQRulesHeight, 0);
    // The 100-block chain was mined to a P2MR output.
    BOOST_REQUIRE_EQUAL(Tip(chainman)->nHeight, 100);
    BOOST_CHECK(m_coinbase_txns.at(0)->vout.at(0).scriptPubKey == m_coinbase_script);

    // A block paying its reward to a P2PKH output is invalid.
    {
        const BranchMiner miner{chainman};
        const CScript p2pkh{CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 1) << OP_EQUALVERIFY << OP_CHECKSIG};
        const CBlock block{miner.Make(Tip(chainman), p2pkh, 0x77)};
        LOCK(::cs_main);
        const BlockValidationState state{TestBlockValidity(chainman.ActiveChainstate(), block, /*check_pow=*/true, /*check_merkle_root=*/true)};
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-output-not-pq");
    }

    // Spend the first (mature) coinbase with an ML-DSA signature.
    const auto coinbase_key{pqch_test::MLDSAKey(COINBASE_PQ_KEY_TAG)};
    const auto coinbase_tree{pqch_test::MakeTree({pqch_test::LeafMLDSA(coinbase_key.pubkey)})};
    const CTransactionRef& funding{m_coinbase_txns.at(0)};
    const auto dest_key{pqch_test::MLDSAKey(0x21)};
    const auto dest_tree{pqch_test::MakeTree({pqch_test::LeafMLDSA(dest_key.pubkey)})};

    auto make_spend{[&](const CScript& output_script) {
        CMutableTransaction tx;
        tx.version = 2;
        tx.vin.emplace_back(COutPoint{funding->GetHash(), 0});
        tx.vout.emplace_back(funding->vout[0].nValue - 100'000, output_script);
        const uint256 sighash{pqch_test::LeafSigHash(tx, 0, {funding->vout[0]}, coinbase_tree.LeafHash(0), SIGHASH_DEFAULT,
                                                     static_cast<uint8_t>(PQAlgorithm::ML_DSA_44))};
        const CScript& leaf{coinbase_tree.leaves[0]};
        tx.vin[0].scriptWitness.stack = {pqch_test::SignMLDSA(coinbase_key.seckey, sighash),
                                         std::vector<unsigned char>(leaf.begin(), leaf.end()), coinbase_tree.Control(0)};
        return tx;
    }};

    // To a witness v0 output: refused by the mempool as a consensus violation.
    {
        const auto tx{make_spend(CScript() << OP_0 << std::vector<unsigned char>(20, 2))};
        const auto result{WITH_LOCK(::cs_main, return chainman.ProcessTransaction(MakeTransactionRef(tx)))};
        BOOST_CHECK(result.m_result_type == MempoolAcceptResult::ResultType::INVALID);
        BOOST_CHECK_EQUAL(result.m_state.GetRejectReason(), "bad-txns-output-not-pq");
    }

    // To a P2MR output: accepted and mined.
    const auto tx{make_spend(dest_tree.ScriptPubKey())};
    const auto result{WITH_LOCK(::cs_main, return chainman.ProcessTransaction(MakeTransactionRef(tx)))};
    BOOST_CHECK_MESSAGE(result.m_result_type == MempoolAcceptResult::ResultType::VALID, result.m_state.ToString());
    BOOST_CHECK(tx.vin[0].scriptWitness.stack[0].size() == pqc::MLDSA44_SIG_SIZE);

    const CBlock block{CreateAndProcessBlock({tx}, m_coinbase_script)};
    BOOST_CHECK_EQUAL(Tip(chainman)->GetBlockHash().GetHex(), block.GetHash().GetHex());
    BOOST_CHECK_EQUAL(block.vtx.size(), 2U);
    const Coin& coin{WITH_LOCK(::cs_main, return chainman.ActiveChainstate().CoinsTip().AccessCoin(COutPoint{tx.GetHash(), 0}))};
    BOOST_CHECK(!coin.IsSpent());
    BOOST_CHECK(coin.out.scriptPubKey == dest_tree.ScriptPubKey());
}

BOOST_AUTO_TEST_SUITE_END()
