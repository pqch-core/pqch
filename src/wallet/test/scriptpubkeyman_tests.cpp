// Copyright (c) 2020-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <key_io.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <script/solver.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(scriptpubkeyman_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(DescriptorScriptPubKeyManTests)
{
    std::unique_ptr<interfaces::Chain>& chain = m_node.chain;

    CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
    auto key_scriptpath = GenerateRandomKey();

    // Verify that a SigningProvider for a pubkey is only returned if its corresponding private key is available
    auto key_internal = GenerateRandomKey();
    std::string desc_str = "tr(" + EncodeSecret(key_internal) + ",pk(" + HexStr(key_scriptpath.GetPubKey()) + "))";
    auto spk_man1 = CreateDescriptor(keystore, desc_str, true);
    BOOST_CHECK(spk_man1 != nullptr);
    auto signprov_keypath_spendable = spk_man1->GetSigningProvider(key_internal.GetPubKey());
    BOOST_CHECK(signprov_keypath_spendable != nullptr);

    desc_str = "tr(" + HexStr(XOnlyPubKey::NUMS_H) + ",pk(" + HexStr(key_scriptpath.GetPubKey()) + "))";
    auto spk_man2 = CreateDescriptor(keystore, desc_str, true);
    BOOST_CHECK(spk_man2 != nullptr);
    auto signprov_keypath_nums_h = spk_man2->GetSigningProvider(XOnlyPubKey::NUMS_H.GetEvenCorrespondingCPubKey());
    BOOST_CHECK(signprov_keypath_nums_h == nullptr);
}

BOOST_AUTO_TEST_CASE(desc_spkm_topup_fail)
{
    // Attempting to construct a DescriptorSPKM that cannot be topped up (hardened derivation without private keys)
    // should fail even though it is valid and can be parsed.
    // PQCH: AddWalletDescriptor returns the error (it runs in a database transaction) instead of throwing.
    CExtKey extkey;
    extkey.SetSeed(std::array<std::byte, 32>{});
    CWallet keystore(m_node.chain.get(), "", CreateMockableWalletDatabase());
    keystore.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    FlatSigningProvider keys;
    std::string error;
    auto descs{Parse("wpkh(" + EncodeExtPubKey(extkey.Neuter()) + "/*h)", keys, error, /*require_checksum=*/false)};
    BOOST_REQUIRE_EQUAL(descs.size(), 1U);
    WalletDescriptor w_desc{std::move(descs.at(0)), /*creation_time=*/1, /*range_start=*/0, /*range_end=*/1, /*next_index=*/0};
    LOCK(keystore.cs_wallet);
    const auto res{keystore.AddWalletDescriptor(w_desc, keys, "", false)};
    BOOST_REQUIRE(!res);
    BOOST_CHECK_EQUAL(util::ErrorString(res).original, "Could not top up scriptPubKeys");
    BOOST_CHECK(keystore.GetAllScriptPubKeyMans().empty());
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
