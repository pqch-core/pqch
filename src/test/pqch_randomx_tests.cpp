// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: tests for the RandomX v2 proof of work.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <pow_randomx.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

namespace {
std::string RxHex(const std::string& key, const std::string& input)
{
    const uint256 h{RandomXHash(MakeUCharSpan(key), MakeUCharSpan(input))};
    return HexStr(std::span<const unsigned char>{h.data(), h.size()});
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_randomx_tests, BasicTestingSetup)

// Official RandomX v2 test vectors (tevador/RandomX v2.0.1, src/tests/tests.cpp, tests a-d).
BOOST_AUTO_TEST_CASE(randomx_v2_official_vectors)
{
    BOOST_CHECK_EQUAL(RxHex("test key 000", "This is a test"),
                      "22ec6b861b3eb23686b2efbad69513c967ecfce80983df66c9c5b4fbfb4cdb6f");
    BOOST_CHECK_EQUAL(RxHex("test key 000", "Lorem ipsum dolor sit amet"),
                      "9e2c772c12fd48f93c14c97fdc89d556264d9100597023f44d9163e279012ecf");
    BOOST_CHECK_EQUAL(RxHex("test key 000", "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua"),
                      "4d6b063a1a603751d525f18a171336a4002f2f06df6c17e4b25fe17e17796e42");
    // Different key, same input: exercises the cache switch.
    BOOST_CHECK_EQUAL(RxHex("test key 001", "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua"),
                      "97024134686ce27d362ea8d86d8ef16483ac272abdabd46ef13359400777fe5e");
    // Back to the first key (still cached): result unchanged.
    BOOST_CHECK_EQUAL(RxHex("test key 000", "This is a test"),
                      "22ec6b861b3eb23686b2efbad69513c967ecfce80983df66c9c5b4fbfb4cdb6f");
}

BOOST_AUTO_TEST_CASE(randomx_mainnet_params)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    BOOST_CHECK(params.fPowUseRandomX);
    BOOST_CHECK_EQUAL(params.nRandomXSeedInterval, 4096);
    BOOST_CHECK_EQUAL(params.nRandomXSeedLag, 64);
    BOOST_CHECK_EQUAL(UintToArith256(params.powLimit).GetCompact(), 0x1e7fffffU);
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().nBits, 0x1e7fffffU);
}

// Key schedule: genesis up to height 4160, then a new key every 4096 blocks, 64 blocks late.
BOOST_AUTO_TEST_CASE(randomx_seed_schedule)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    BOOST_CHECK_EQUAL(GetRandomXSeedHeight(1, params), 0);
    BOOST_CHECK_EQUAL(GetRandomXSeedHeight(4160, params), 0);
    BOOST_CHECK_EQUAL(GetRandomXSeedHeight(4161, params), 4096);
    BOOST_CHECK_EQUAL(GetRandomXSeedHeight(8256, params), 4096);
    BOOST_CHECK_EQUAL(GetRandomXSeedHeight(8257, params), 8192);
    for (int h = 4161; h < 100'000; h += 37) {
        const int s{GetRandomXSeedHeight(h, params)};
        BOOST_CHECK_EQUAL(s % 4096, 0);
        BOOST_CHECK(s <= h - 65);       // the key block is at least 65 blocks deep
        BOOST_CHECK(h - s <= 4096 + 64); // and never more than one epoch plus the lag
    }
}

// The seed is the hash of the right ancestor.
BOOST_AUTO_TEST_CASE(randomx_seed_hash)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& params = chainParams->GetConsensus();
    std::vector<uint256> hashes(9000);
    std::vector<CBlockIndex> blocks(9000);
    for (int i = 0; i < 9000; ++i) {
        hashes[i] = ArithToUint256(arith_uint256(i + 1));
        blocks[i].phashBlock = &hashes[i];
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].BuildSkip();
    }
    BOOST_CHECK_EQUAL(GetRandomXSeedHash(&blocks[100], params), hashes[0]);
    BOOST_CHECK_EQUAL(GetRandomXSeedHash(&blocks[4160], params), hashes[4096]); // block 4161
    BOOST_CHECK_EQUAL(GetRandomXSeedHash(&blocks[8256], params), hashes[8192]); // block 8257
}

// A header passes only when its RandomX hash meets the target; SHA256d is irrelevant.
BOOST_AUTO_TEST_CASE(randomx_check_block_pow)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const auto& params = chainParams->GetConsensus();
    BOOST_CHECK(params.fPowUseRandomX);

    const CBlock& genesis = chainParams->GenesisBlock();
    uint256 genesis_hash{genesis.GetHash()};
    CBlockIndex genesis_index{genesis};
    genesis_index.phashBlock = &genesis_hash;
    genesis_index.nHeight = 0;

    CBlockHeader header;
    header.nVersion = 4;
    header.hashPrevBlock = genesis_hash;
    header.nTime = genesis.nTime + 60;
    header.nBits = 0x1f00ffff; // ~256 hashes on average: makes failures common and successes cheap

    int passes{0}, fails{0};
    for (header.nNonce = 0; header.nNonce < 2000 && (passes < 3 || fails < 3); ++header.nNonce) {
        const uint256 rx{GetRandomXPowHash(header, genesis_hash)};
        const bool expected{CheckProofOfWork(rx, header.nBits, params)};
        BOOST_CHECK_EQUAL(CheckBlockProofOfWork(header, &genesis_index, params), expected);
        (expected ? passes : fails)++;
    }
    BOOST_CHECK(passes >= 3);
    BOOST_CHECK(fails >= 3);

    // Changing the key changes the hash.
    BOOST_CHECK(GetRandomXPowHash(header, genesis_hash) != GetRandomXPowHash(header, uint256::ONE));
}

BOOST_AUTO_TEST_SUITE_END()
