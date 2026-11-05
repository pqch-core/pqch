// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: importing a post-quantum wallet descriptor is one database transaction.

#include <random.h>
#include <script/descriptor.h>
#include <script/p2mr.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <string>

namespace wallet {
namespace {
/** Record count by key type ("walletdescriptor", "walletdescriptorpqseed", ...). */
std::map<std::string, int> RecordTypes(CWallet& wallet)
{
    std::map<std::string, int> types;
    auto batch{wallet.GetDatabase().MakeBatch()};
    auto cursor{batch->GetNewCursor()};
    DataStream key, value;
    while (cursor->Next(key, value) == DatabaseCursor::Status::MORE) {
        std::string type;
        key >> type;
        ++types[type];
        key.clear();
        value.clear();
    }
    return types;
}

p2mr::PQSeed RandomSeed()
{
    const auto b{GetRandHash()};
    return p2mr::PQSeed{std::span<const unsigned char>{b.begin(), b.size()}};
}

WalletDescriptor ParseP2MR(const uint256& seed_id, FlatSigningProvider& keys)
{
    std::string error;
    auto descs{Parse("p2mr(pqseed(" + HexStr(seed_id) + ")/360h/1h/0h/0h/*h)", keys, error, /*require_checksum=*/false)};
    BOOST_REQUIRE_MESSAGE(descs.size() == 1, error);
    return WalletDescriptor{std::move(descs.at(0)), /*creation_time=*/0, /*range_start=*/0, /*range_end=*/0, /*next_index=*/0};
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_wallet_import_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(p2mr_import_is_atomic)
{
    CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
    LOCK(wallet.cs_wallet);
    wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet.m_keypool_size = 3;

    const p2mr::PQSeed seed{RandomSeed()}, other{RandomSeed()};

    // The descriptor names one seed, the import carries another: the other seed is written first,
    // then generating the addresses fails. Nothing may stay behind, on disk or in memory.
    {
        FlatSigningProvider keys;
        keys.pq_seeds.emplace(other.Id(), other);
        WalletDescriptor w_desc{ParseP2MR(seed.Id(), keys)};
        const auto before{RecordTypes(wallet)};
        BOOST_CHECK(!wallet.AddWalletDescriptor(w_desc, keys, "", false));
        BOOST_CHECK(RecordTypes(wallet) == before);
        BOOST_CHECK(wallet.GetAllScriptPubKeyMans().empty());
    }

    // With the right seed: descriptor, seed and address cache are all written.
    {
        FlatSigningProvider keys;
        keys.pq_seeds.emplace(seed.Id(), seed);
        WalletDescriptor w_desc{ParseP2MR(seed.Id(), keys)};
        auto spkm{wallet.AddWalletDescriptor(w_desc, keys, "", false)};
        BOOST_REQUIRE(spkm);
        const auto types{RecordTypes(wallet)};
        BOOST_CHECK_EQUAL(types.count("walletdescriptor") ? types.at("walletdescriptor") : 0, 1);
        BOOST_CHECK_EQUAL(types.count("walletdescriptorpqseed") ? types.at("walletdescriptorpqseed") : 0, 1);
        BOOST_CHECK(types.count("walletdescriptorp2mrcache") && types.at("walletdescriptorp2mrcache") >= 3);
        BOOST_CHECK_EQUAL(wallet.GetAllScriptPubKeyMans().size(), 1U);
        for (const CScript& script : spkm->get().GetScriptPubKeys()) BOOST_CHECK(wallet.IsMine(script));
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
