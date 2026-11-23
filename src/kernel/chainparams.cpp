// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <crypto/hex_base.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/verify_flags.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/log.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <span>
#include <utility>

using namespace util::hex_literals;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * PQCH: build a PQCH genesis block. The single output is OP_RETURN "PQCH", so it is
 * unspendable by construction (no public key of any kind is involved).
 */
static CBlock CreatePQCHGenesisBlock(const char* pszTimestamp, uint32_t nTime, uint32_t nNonce, uint32_t nBits, const CAmount& genesisReward)
{
    const CScript genesisOutputScript = CScript() << OP_RETURN << std::vector<unsigned char>{'P', 'Q', 'C', 'H'};
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, /*nVersion=*/1, genesisReward);
}

void CChainParams::ApplyDeploymentOptions(const DeploymentOptions& opts)
{
    for (const auto& [dep, height] : opts.activation_heights) {
        switch (dep) {
        case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
            consensus.SegwitHeight = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
            consensus.BIP34Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
            consensus.BIP66Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
            consensus.BIP65Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
            consensus.CSVHeight = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_PQRULES:
            consensus.PQRulesHeight = int{height};
            break;
        }
    }

    for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
        consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
        consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
        consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
    }
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams(const MainNetOptions& opts) {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        // PQCH: 7 PQCH per block, halved every 4,207,000 blocks (~8 years at 1 block per minute):
        // 58,897,999.50 PQCH in total, under MAX_MONEY (whitepaper, section 26.1).
        consensus.nSubsidyHalvingInterval = 4207000;
        consensus.nInitialSubsidy = 7 * COIN;
        // PQCH: new chain, so no historical script flag exceptions and all soft forks active from the start.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        // PQCH: post-quantum rules from the genesis block: no output is ever protected by an
        // elliptic-curve key alone (whitepaper, section 21).
        consensus.PQRulesHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        // PQCH: RandomX runs at thousands, not trillions, of hashes per second per machine, so the
        // easiest target is 2^239 - 1 (about 131,072 hashes per block, i.e. ~2.2 kH/s of total
        // network hashrate for 1-minute blocks). 2^239 is the largest value for which ASERT's
        // 256-bit arithmetic cannot overflow.
        consensus.powLimit = uint256{"00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        // PQCH: RandomX v2 proof of work; key changes every 4096 blocks with a 64-block lag.
        consensus.fPowUseRandomX = true;
        consensus.nRandomXSeedInterval = 4096;
        consensus.nRandomXSeedLag = 64;
        // PQCH: merged mining with Monero from the genesis block.
        consensus.nAuxPowHeight = 0;
        // PQCH: 1-minute blocks with ASERT difficulty adjustment on every block, anchored at genesis.
        // nPowTargetTimespan is no longer used for retargeting when ASERT is enabled.
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 60;
        consensus.fPowUseASERT = true;
        consensus.nASERTHalfLife = 4 * 60 * 60; // 4 hours (240 blocks); calibrated 23 Nov 2026 (contrib/pqch/CALIBRACAO.md)
        consensus.nASERTAnchorHeight = 0;
        // PQCH: 720 blocks (~12 h) before a block reward can be spent.
        consensus.nCoinbaseMaturity = MAINNET_COINBASE_MATURITY;
        // PQCH: a synced node never rewrites more than 60 blocks (~1 h) of its active chain. Calibrated
        // 23 Nov 2026 (contrib/pqch/CALIBRACAO.md).
        m_max_reorg_depth = 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
        // PQCH: ML-DSA emergency soft fork, ready from genesis: 90% of a day of blocks (1,296 of 1,440)
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].threshold = 1296;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].period = 1440;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].signal_by_default = false; // only with -signalmldsaemergency

        ApplyDeploymentOptions(opts.dep_opts);

        // PQCH: new chain, no accumulated work or assumed-valid block yet.
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};
        // Header checkpoints (pow_randomx.h): none until the chain exists; each release adds them with
        // contrib/pqch/checkpoints.py, checked on two nodes.

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        // PQCH: randomly generated, distinct from Bitcoin and its test networks.
        pchMessageStart[0] = 0xa7;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0xdc;
        pchMessageStart[3] = 0xbf;
        nDefaultPort = 29333; // PQCH: provisional, to be checked against other networks before launch
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        // TODO(PQCH launch): PROVISIONAL genesis for development. The definitive genesis is created on
        // launch day (decided 23 Nov 2026, contrib/pqch/GENESIS.md): a headline of that day as its
        // message; nTime = the announced start time, to the minute (no block can then be mined more
        // than MAX_FUTURE_BLOCK_TIME before it); nBits 0x1e7fffff (powLimit, ~2.2 kH/s), ASERT adjusts
        // in the first hours; reward 7 PQCH (unspendable OP_RETURN). Its bits also anchor ASERT.
        genesis = CreatePQCHGenesisBlock("PQCH provisional mainnet genesis 04/Nov/2026 - replaced at launch", 1793800800, 0, 0x1e7fffff, 1'250'000'000);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"047f81331ca9e86897b2d1eae7747f78352e9d566ddff94c2acdbac5c0bbee8a"});
        assert(genesis.hashMerkleRoot == uint256{"7707b9c39887615a04309653c2a090def013d3f2fbb436d1836a741b1b98c270"});

        // PQCH: DNS seeds and fixed seeds are added before launch.
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // PQCH: addresses start with "pqc1".
        bech32_hrp = "pqc";

        vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // PQCH: no assumeutxo snapshots yet.
        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 0,
            .tx_count = 0,
            .dTxRate  = 0,
        };

        // Generated by headerssync-params.py on 2026-09-08.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400, // 15400/649 = ~23.7 commitments
        };
    }
};

/**
 * PQCH testnet: public test network, same rules as the main network, coins without value. It can be
 * restarted with a new genesis block.
 */
// PQCH: the genesis message is a headline of The Times on the day the testnet was created (the
// Bitcoin tradition); it proves the chain did not exist before that day.
// Lead headline of thetimes.com on 06/Nov/2026 (sent by the project lead from the site).
static constexpr const char* PQCH_TESTNET_GENESIS_MESSAGE{"The Times 06/Nov/2026 Vance has an awkward problem and little time to solve it"};
static constexpr uint32_t PQCH_TESTNET_GENESIS_TIME{1793923200}; // 2026-11-06 00:00:00 UTC

class CTestNetParams : public CChainParams {
public:
    CTestNetParams(const TestNetOptions& opts) {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        // PQCH: the main network's consensus rules (see CMainParams), so that what is tested here is
        // what will run there.
        consensus.nSubsidyHalvingInterval = 4207000;
        consensus.nInitialSubsidy = 7 * COIN;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.PQRulesHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.fPowUseRandomX = true;
        consensus.nRandomXSeedInterval = 4096;
        consensus.nRandomXSeedLag = 64;
        consensus.nAuxPowHeight = 0;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 60;
        consensus.fPowUseASERT = true;
        consensus.nASERTHalfLife = 4 * 60 * 60; // 4 hours, as on the main network (calibrated 23 Nov 2026)
        consensus.nASERTAnchorHeight = 0;
        // PQCH: 100 blocks (~100 min) instead of the main network's 720, so that testers can spend
        // what they mine the same day.
        consensus.nCoinbaseMaturity = COINBASE_MATURITY;
        m_max_reorg_depth = 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
        // PQCH: ML-DSA emergency soft fork, as on mainnet
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].threshold = 1296;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].period = 1440;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].signal_by_default = false; // only with -signalmldsaemergency

        ApplyDeploymentOptions(opts.dep_opts);

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};
        // Header checkpoints: every 500 blocks up to 22000, contrib/pqch/checkpoints.py
        consensus.header_checkpoints = {
            {500, uint256{"00e2b9b4c19fc3a0cfb67df5154a5c7ee6ea502be0a548125f160c9e9ef980aa"}},
            {1000, uint256{"fd1b2e03019100f0e0d293c74ae6bcac517c0353c01fb3e107cb36bf939daa22"}},
            {1500, uint256{"4d13a4e74a23696ba49ef08a930377461ab539faf764ddf74e6f594df43c8348"}},
            {2000, uint256{"2812182a91efa965397a01ee4a4755b7090a43a386b89042096eec4281467e2d"}},
            {2500, uint256{"1807f2b4f6929ffb899e1a03077af7df3b9a891a2c20b611a15a4e420e4ddd6c"}},
            {3000, uint256{"499aca434f9cb93265a1bfc7e6ea1253fb12eb9a1c64a6d4d19b706b10c38643"}},
            {3500, uint256{"87da4edea61ab275044639bf5b2fa305085a11e787ce8a17da54ee760c770614"}},
            {4000, uint256{"fa6afe46d5dac97ebe24806e6f9bce60af7e68b936b4c9a21474e32d9747d76d"}},
            {4500, uint256{"35a5bb50601b8fc971ec6c93599800dc33f0cb2419818e6000207b5f38f7f828"}},
            {5000, uint256{"decf3f254b9984dd0fe7851ea94b187af196791fa63b8ae996d09c36b220adb0"}},
            {5500, uint256{"c201e5ca8e9f8bf7cd0b87ab755b38ababb721ab11151eff5b386eee7636d371"}},
            {6000, uint256{"6c74e466d7dedc14eeb1c2af031919fbe3b360c299be579fea6c5e6932fb5192"}},
            {6500, uint256{"e50ee0dca19481b83a126b3d31694fe31d8b6a69c2d81d41d4ec47f562a857fd"}},
            {7000, uint256{"6f7d9688386ee275018dc75cf413966fe553d8353e0f2e1de39475607ce7b179"}},
            {7500, uint256{"ca4f8d903776b1fd86169e4d8f67af097286508fd2f7eb4725e6fb690e38bab9"}},
            {8000, uint256{"65300b420d672219287c06ac89ca4031ecbf28114f698958af49c04fc84b5156"}},
            {8500, uint256{"c1123fa5cadb5eaa737a26b6ba3b7fc400c4b5cadd63dae52565d3f903dd9798"}},
            {9000, uint256{"37233dec01e5fdded6304bbd8cb2a8fbd8516613162362116cb438f820d6ec48"}},
            {9500, uint256{"02be0eb4e3adf248078d592a020a6f44c6426eeee615e9daa913efa31929b649"}},
            {10000, uint256{"ffc4b15c7718e973f33d667a6a5611519ade1e1cb07fcc8a4c6ff46f80ace2fd"}},
            {10500, uint256{"08a3a76da5b87016035612d9d8d9bdc48777366b7944b49dc7130a9f1b331911"}},
            {11000, uint256{"491af0ee31dbd8e3eda78f9edbad6ce939fa662cfd490ec6c4c254669e0aba71"}},
            {11500, uint256{"311ae433e335d054f5b7591e6295cb0189fcbdf8fc708e9b80251fcb420a7bb3"}},
            {12000, uint256{"4518e827c0a7e78da151ca8d950973a8304a7b358eea7ae490ce87b8d2b35358"}},
            {12500, uint256{"6999a9800f1fd31c6bdbe5289b62dd955efd2c36c131ca40b7ce2f98ed75ff1a"}},
            {13000, uint256{"a7423690ad4736e723fa70a8adfc95ee509f1f1e3e0ce6946896a8c5bb200d51"}},
            {13500, uint256{"e91c2efaab42886596de03d89f0bdb119f10f3a0772a8fc6493de9628cd57ee2"}},
            {14000, uint256{"9fd4531e65814f55a35779bd79dccea8fe91e1ee46259da91e3e98c069c94b01"}},
            {14500, uint256{"348e1c3b8377d2ec1f0f25d1056193d81b765f860c4d95fc049b101f5079a804"}},
            {15000, uint256{"4e635feb92ae7d560f782ad32e373b520220d06a530e6e5e03968635460741ff"}},
            {15500, uint256{"8bfafd121be3447a381f4ed2daf3bff35634137243a43ec3494bce2be4d4dffc"}},
            {16000, uint256{"3010bc555d61f8f4c7f6cfe6a9279a84d53b9a88eef5232739e542742851ef5e"}},
            {16500, uint256{"63bee8531c09780da2db53e0150a2d3a5958c2d660770fea749b20c6034ab7d5"}},
            {17000, uint256{"f9f7eece96b66e7c8bf1f17d7abfda57971ee42e0aac94bef2c071f5477bad8c"}},
            {17500, uint256{"efa3198c99138aed1112693693f45ebc8143baf5a285148b99094bd2b931ed34"}},
            {18000, uint256{"19898f1bc23cb33369769847658d2d76990f0070d3db4fba9fc807dbec0fcf48"}},
            {18500, uint256{"feccad418eb63835fb56cd0e08d6824cbf3265083f978621cb9329a664e5279d"}},
            {19000, uint256{"415a0adc82a694e78718a35b9d966cce6de405a30441955aeb67ae2d6ed5d7d1"}},
            {19500, uint256{"25b16733a93e263d415b44dbe781f453d5c8075e04759a84267ad9247840007c"}},
            {20000, uint256{"d05948d6c433cf71b7e9cbd2e6275ab3a26e6b04d853ca063af276f2d4e23bb1"}},
            {20500, uint256{"3f59e24e1ed2c6322980dc124cab4a9a79781a1b44e77d36d4f60b42d98a872f"}},
            {21000, uint256{"c2c5fdcca4655acb075f8c67712b21da623ccfc9615f47b11e238e25480fadf6"}},
            {21500, uint256{"0ac217def8e4bb997bca1c4842a6a0710769bf156231fe2688b7b28beee57f2c"}},
            {22000, uint256{"ae428de6a2131101130edd106d61fb8b77733638f138bfbcf791608feddf108d"}},
        };

        // PQCH: randomly generated, distinct from the main network, regtest and Bitcoin's networks.
        pchMessageStart[0] = 0xe8;
        pchMessageStart[1] = 0xf4;
        pchMessageStart[2] = 0xed;
        pchMessageStart[3] = 0x9b;
        nDefaultPort = 39333; // PQCH testnet (RPC 39332)
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        genesis = CreatePQCHGenesisBlock(PQCH_TESTNET_GENESIS_MESSAGE, PQCH_TESTNET_GENESIS_TIME, 0, 0x1e7fffff, 1'250'000'000);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"b3079ef1e4520057cda8cfb841850523d295519c1815a8e3287e2a9c465d9caa"});
        assert(genesis.hashMerkleRoot == uint256{"c39254a1cdab114a1e14d2fe8a31d686801052099170e132db6b2109109ab93c"});

        // PQCH: the project's public testnet nodes (contrib/seeds/nodes_test.txt, TESTNET.md). No DNS
        // seed yet.
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // PQCH: testnet addresses start with "pqct1".
        bech32_hrp = "pqct";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 0,
            .tx_count = 0,
            .dTxRate  = 0,
        };

        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400,
        };
    }
};

/**
 * Testnet (v4): public test network which is reset from time to time.
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params(const TestNetOptions& opts) {
        m_chain_type = ChainType::TESTNET4;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
        // PQCH: ML-DSA emergency soft fork, not used on this inherited network
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].threshold = 1815;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].period = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].signal_by_default = false; // only with -signalmldsaemergency

        ApplyDeploymentOptions(opts.dep_opts);

        consensus.nMinimumChainWork = uint256{"000000000000000000000000000000000000000000000e346a558455ade8eca9"};
        consensus.defaultAssumeValid = uint256{"0000000021df65b91665a342e26ceb05e54826ad7d8fcd40316230058fa3b865"}; // 151604

        pchMessageStart[0] = 0x1c;
        pchMessageStart[1] = 0x16;
        pchMessageStart[2] = 0x3f;
        pchMessageStart[3] = 0x28;
        nDefaultPort = 48333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 31;
        m_assumed_chain_state_size = 2;

        const char* testnet4_genesis_msg = "03/May/2024 000000000000000000001ebd58c244970b3aa9d783bb001011fbe8ea8e98e00e";
        const CScript testnet4_genesis_script = CScript() << "000000000000000000000000000000000000000000000000000000000000000000"_hex << OP_CHECKSIG;
        genesis = CreateGenesisBlock(testnet4_genesis_msg,
                testnet4_genesis_script,
                1714777860,
                393743547,
                0x1d00ffff,
                1,
                50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"});
        assert(genesis.hashMerkleRoot == uint256{"7aa0a7ae1e223414cb807e40cd57e667b718e42aaf9306db9102fe28912b7b4e"});

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("seed.testnet4.bitcoin.sprovoost.nl."); // Sjors Provoost
        vSeeds.emplace_back("seed.testnet4.wiz.biz."); // Jason Maurice

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_testnet4), std::end(chainparams_seed_testnet4));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {
            {
                .height = 90'000,
                .hash_serialized = AssumeutxoHash{uint256{"784fb5e98241de66fdd429f4392155c9e7db5c017148e66e8fdbc95746f8b9b5"}},
                .m_chain_tx_count = 11347043,
                .blockhash = uint256{"0000000002ebe8bcda020e0dd6ccfbdfac531d2f6a81457191b99fc2df2dbe3b"},
            },
            {
                .height = 120'000,
                .hash_serialized = AssumeutxoHash{uint256{"10b05d05ad468d0971162e1b222a4aa66caca89da2bb2a93f8f37fb29c4794b0"}},
                .m_chain_tx_count = 14141057,
                .blockhash = uint256{"000000000bd2317e51b3c5794981c35ba894ce27d3e772d5c39ecd9cbce01dc8"},
            },
            {
                .height = 150'000,
                .hash_serialized = AssumeutxoHash{uint256{"ca068cae50679d7c947454bbe4f0e6aeec1fbe2c6c2735a08bb988623649f950"}},
                .m_chain_tx_count = 14810011,
                .blockhash = uint256{"0000000000d9877342754dea8ec1eb24631517d38e3443c370465ee53a8b7434"},
            }
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 0000000021df65b91665a342e26ceb05e54826ad7d8fcd40316230058fa3b865
            .nTime    = 1788932739,
            .tx_count = 14825119,
            .dTxRate  = 0.1148250171344748,
        };

        // Generated by headerssync-params.py on 2026-09-08.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 616,
            .redownload_buffer_size = 16199, // 16199/616 = ~26.3 commitments
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vFixedSeeds.clear();
        vSeeds.clear();

        if (!options.challenge) {
            bin = "512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae"_hex_v_u8;
            vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_signet), std::end(chainparams_seed_signet));
            vSeeds.emplace_back("seed.signet.bitcoin.sprovoost.nl.");
            vSeeds.emplace_back("seed.signet.achownodes.xyz."); // Ava Chow, only supports x1, x5, x9, x49, x809, x849, xd, x400, x404, x408, x448, xc08, xc48, x40c

            consensus.nMinimumChainWork = uint256{"00000000000000000000000000000000000000000000000000001090e9dc1520"};
            consensus.defaultAssumeValid = uint256{"00000002a5e0ba0498f1e9f4591af0b66b63c654665efe65206fd0ae7bbaf923"}; // 321295
            m_assumed_blockchain_size = 25;
            m_assumed_chain_state_size = 5;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 00000002a5e0ba0498f1e9f4591af0b66b63c654665efe65206fd0ae7bbaf923
                .nTime    = 1788925860,
                .tx_count = 32273793,
                .dTxRate  = 0.3977566114505673,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogInfo("Signet with challenge %s", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000377ae000000000000000000000000000000000000000000000000000000"};
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
        // PQCH: ML-DSA emergency soft fork, not used on this inherited network
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].threshold = 1815;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].period = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].signal_by_default = false; // only with -signalmldsaemergency

        ApplyDeploymentOptions(options.dep_opts);

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1598918400, 52613770, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6"});
        assert(genesis.hashMerkleRoot == uint256{"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"});

        m_assumeutxo_data = {
            {
                .height = 160'000,
                .hash_serialized = AssumeutxoHash{uint256{"fe0a44309b74d6b5883d246cb419c6221bcccf0b308c9b59b7d70783dbdf928a"}},
                .m_chain_tx_count = 2289496,
                .blockhash = uint256{"0000003ca3c99aff040f2563c2ad8f8ec88bd0fd6b8f0895cfaf1ef90353a62c"},
            },
            {
                .height = 290'000,
                .hash_serialized = AssumeutxoHash{uint256{"97267e000b4b876800167e71b9123f1529d13b14308abec2888bbd2160d14545"}},
                .m_chain_tx_count = 28547497,
                .blockhash = uint256{"0000000577f2741bb30cd9d39d6d71b023afbeb9764f6260786a97969d5c9ac0"},
            },
            {
                .height = 320'000,
                .hash_serialized = AssumeutxoHash{uint256{"1aaf72ecb376cc16957fbb8d5d406bfd6e3165510e2fc83879b6d14cd20b4462"}},
                .m_chain_tx_count = 32079110,
                .blockhash = uint256{"0000000740ae66b284da84387dcfa14d7b1385b0bad482005ba4e770ea6c4b95"},
            }
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // Generated by headerssync-params.py on 2026-09-08.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 629,
            .redownload_buffer_size = 15885, // 15885/629 = ~25.3 commitments
        };
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.PQRulesHeight = 0; // PQCH: always active unless overridden (-testactivationheight=pqrules@<height>)
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        // PQCH: 144 one-minute blocks per "retarget period", as in Bitcoin's regtest (no retargeting
        // happens on regtest; this only sets DifficultyAdjustmentInterval() for BIP94 and RPC tests).
        consensus.nPowTargetTimespan = 144 * 60;
        consensus.nPowTargetSpacing = 60; // PQCH: 1-minute blocks
        consensus.fPowUseRandomX = true; // PQCH: RandomX v2 proof of work (trivial target, ~2 hashes per block)
        consensus.nAuxPowHeight = 0;     // PQCH: merged mining with Monero
        // PQCH: regtest keeps Bitcoin's 100-block coinbase maturity so that test chains stay short;
        // the reorg limit is the main network's (tests can change it with -maxreorgdepth).
        consensus.nCoinbaseMaturity = COINBASE_MATURITY;
        m_max_reorg_depth = 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = opts.enforce_bip94;
        consensus.header_checkpoints = opts.header_checkpoints;
        consensus.fPowNoRetargeting = true;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 108; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 144; // Faster than normal for regtest (144 instead of 2016)
        // PQCH: ML-DSA emergency soft fork, 90% of 144 blocks; -vbparams=mldsaemergency:... to change
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].threshold = 130;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].period = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_MLDSA_EMERGENCY].signal_by_default = false; // only with -signalmldsaemergency

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        // PQCH regtest: randomly generated message start.
        pchMessageStart[0] = 0xa2;
        pchMessageStart[1] = 0xf4;
        pchMessageStart[2] = 0x92;
        pchMessageStart[3] = 0xd4;
        nDefaultPort = 49444;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        ApplyDeploymentOptions(opts.dep_opts);

        // PQCH regtest genesis: reward 12.5 PQCH, OP_RETURN output. The timestamp is Bitcoin regtest's (2011):
        // a genesis older than -maxtipage puts a fresh node in initial block download, as tests expect.
        genesis = CreatePQCHGenesisBlock("PQCH regtest 04/Nov/2026 Peer-to-peer electronic cash, quantum-safe", /*nTime=*/1296688602, 1, 0x207fffff, 1'250'000'000);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"dc23bf8241b903f340911cf5199292b79b6ca42078d45c1dca9ccfe18678364b"});
        assert(genesis.hashMerkleRoot == uint256{"0d1c1bfd8c16966c2f0c7e4a47a387465dd665a10174eb077c589d7a27ef75d7"});

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        // PQCH: snapshot of the deterministic unit test chain (TestChain100Setup + 10 blocks, PQCH
        // genesis and subsidy). Bitcoin's snapshots at heights 200 (fuzz target) and 299 (functional
        // tests) are to be regenerated with those tests.
        m_assumeutxo_data = {
            {   // For use by unit tests
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256{"5d6c74a49ff1d545b52f234e124e76e873ffca54f9b86ae0c75fdab9425ebf61"}},
                .m_chain_tx_count = 111,
                .blockhash = uint256{"bb728fa34c4e19cd72e4bc2e5dcbe1757b0432ebdebda103b779294a8cccc116"},
            },
        };

        chainTxData = ChainTxData{
            .nTime = 0,
            .tx_count = 0,
            .dTxRate = 0.001, // Set a non-zero rate to make it testable
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "pqcrt"; // PQCH regtest addresses start with "pqcrt1"

        // Copied from Testnet4.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 275,
            .redownload_buffer_size = 7017, // 7017/275 = ~25.5 commitments
        };
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main(const MainNetOptions& options)
{
    return std::make_unique<const CMainParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::TestNet(const TestNetOptions& options)
{
    return std::make_unique<const CTestNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::TestNet4(const TestNetOptions& options)
{
    return std::make_unique<const CTestNet4Params>(options);
}

std::vector<int> CChainParams::GetAvailableSnapshotHeights() const
{
    std::vector<int> heights;
    heights.reserve(m_assumeutxo_data.size());

    for (const auto& data : m_assumeutxo_data) {
        heights.emplace_back(data.height);
    }
    return heights;
}

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& message)
{
    const auto mainnet_msg = CChainParams::Main()->MessageStart();
    const auto testnet_msg = CChainParams::TestNet()->MessageStart();
    const auto testnet4_msg = CChainParams::TestNet4()->MessageStart();
    const auto regtest_msg = CChainParams::RegTest()->MessageStart();
    const auto signet_msg = CChainParams::SigNet()->MessageStart();

    if (std::ranges::equal(message, mainnet_msg)) {
        return ChainType::MAIN;
    } else if (std::ranges::equal(message, testnet_msg)) {
        return ChainType::TESTNET;
    } else if (std::ranges::equal(message, testnet4_msg)) {
        return ChainType::TESTNET4;
    } else if (std::ranges::equal(message, regtest_msg)) {
        return ChainType::REGTEST;
    } else if (std::ranges::equal(message, signet_msg)) {
        return ChainType::SIGNET;
    }
    return std::nullopt;
}
