// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/p2mr.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

namespace {

CExtKey MasterKey(uint8_t tag)
{
    std::vector<std::byte> seed(32, std::byte{tag});
    CExtKey master;
    master.SetSeed(seed);
    return master;
}

CKey Key(uint8_t tag)
{
    std::vector<unsigned char> b(32, tag);
    CKey k;
    k.Set(b.begin(), b.end(), true);
    return k;
}

struct Expanded {
    std::vector<CScript> scripts;
    FlatSigningProvider out;
    DescriptorCache cache;
};

/** A transaction spending `spk` (amount 1 PQCH) to an OP_RETURN output. */
CMutableTransaction Spend(const CScript& spk, CTxOut& prev)
{
    prev = CTxOut{COIN, spk};
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    tx.vout.emplace_back(COIN - 10'000, CScript() << OP_RETURN);
    return tx;
}

bool SignAndVerify(CMutableTransaction& tx, const CTxOut& prev, const SigningProvider& provider, const SignOptions& options, SignatureData& sigdata)
{
    PrecomputedTransactionData txdata;
    txdata.Init(tx, {prev}, /*force=*/true);
    MutableTransactionSignatureCreator creator{tx, 0, prev.nValue, &txdata, options};
    if (!ProduceSignature(provider, creator, prev.scriptPubKey, sigdata)) return false;
    UpdateInput(tx.vin[0], sigdata);
    ScriptError err;
    return VerifyScript(tx.vin[0].scriptSig, prev.scriptPubKey, &tx.vin[0].scriptWitness,
                        STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_P2MR,
                        MutableTransactionSignatureChecker{&tx, 0, prev.nValue, txdata, MissingDataBehavior::FAIL}, &err);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_p2mr_wallet_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(p2mr_key_derivation)
{
    const CKey child{Key(1)}, recovery{Key(2)};
    p2mr::AddressKeys a, b;
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(child, recovery, a));
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(child, recovery, b));
    BOOST_CHECK(a == b); // deterministic: the seed recovers every key
    BOOST_CHECK(a.IsValid());
    BOOST_CHECK(a.recovery_owner == recovery.GetPubKey());

    // The Schnorr key is not the BIP32 child key: revealing it (and a quantum attacker recovering
    // its private key) says nothing about the child key, from which the ML-DSA seed comes.
    BOOST_CHECK(a.schnorr != XOnlyPubKey{child.GetPubKey()});
    p2mr::LeafAKeys leaf_a;
    BOOST_REQUIRE(p2mr::DeriveLeafAKeys(child, leaf_a));
    BOOST_CHECK(XOnlyPubKey{leaf_a.schnorr.GetPubKey()} == a.schnorr);
    BOOST_CHECK(leaf_a.mldsa.pubkey == a.mldsa);

    // Another address: other keys and salt, same recovery key.
    p2mr::AddressKeys c;
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(Key(3), recovery, c));
    BOOST_CHECK(c.mldsa != a.mldsa);
    BOOST_CHECK(c.salt != a.salt);
    BOOST_CHECK(c.slhdsa == a.slhdsa);
    const p2mr::SpendData sa{p2mr::MakeSpendData(a, child.GetPubKey())}, sc{p2mr::MakeSpendData(c, Key(3).GetPubKey())};
    // The recovery leaf hash (revealed when leaf A is spent) differs between addresses.
    BOOST_CHECK(sa.LeafHash(true) != sc.LeafHash(true));
    BOOST_CHECK(sa.Root() != sc.Root());

    // The memoised recovery public key equals a fresh derivation.
    pqc::KeyPair slh;
    BOOST_REQUIRE(p2mr::DeriveRecoveryKey(recovery, slh));
    BOOST_CHECK(slh.pubkey == a.slhdsa);

    // Witness sizes of the two paths.
    BOOST_CHECK_EQUAL(p2mr::HybridWitnessSize(false), 3876);
    BOOST_CHECK_EQUAL(p2mr::RecoveryWitnessSize(false), 7947);

    // Fixed vector, so a change to the derivation is noticed (it would lose users' funds).
    BOOST_CHECK_EQUAL(HexStr(a.schnorr), "8b4411e850961a8423112cfe823ac13908f9039f44f6fcbfe782a1007b044ca2");
    BOOST_CHECK_EQUAL(HexStr(a.salt), "e10176172f17d34dbe037e3763bdfff3");
    BOOST_CHECK_EQUAL(sa.Root().GetHex(), "79763a43ead4db13c50f8fe0b0bc6f706bc9db26bc41e01d115f1ff2a6003968");
}

BOOST_AUTO_TEST_CASE(p2mr_descriptor_parse)
{
    const CExtKey master{MasterKey(7)};
    const std::string xprv{EncodeExtKey(master)}, xpub{EncodeExtPubKey(master.Neuter())};
    FlatSigningProvider keys;
    std::string error;

    auto ok{Parse("p2mr(" + xprv + "/360h/0h/0h/0h/*h)", keys, error, /*require_checksum=*/false)};
    BOOST_REQUIRE_MESSAGE(ok.size() == 1, error);
    BOOST_CHECK(ok[0]->IsRange());
    BOOST_CHECK(ok[0]->GetOutputType() == OutputType::BECH32M);
    BOOST_CHECK(ok[0]->ToString().starts_with("p2mr(" + xpub + "/360h/0h/0h/0h/*h)#"));
    BOOST_CHECK_EQUAL(*ok[0]->MaxSatisfactionWeight(false), 3876);

    // Unhardened steps cannot work (no public derivation of post-quantum keys).
    for (const std::string& bad : {"p2mr(" + xpub + "/0/*)", "p2mr(" + xprv + "/360h/*)", "p2mr(" + xprv + "/0h/1/*h)"}) {
        FlatSigningProvider k;
        BOOST_CHECK_MESSAGE(Parse(bad, k, error, false).empty(), bad);
        BOOST_CHECK(error.find("hardened") != std::string::npos);
    }
    // Only at the top level.
    BOOST_CHECK(Parse("sh(p2mr(" + xprv + "/0h))", keys, error, false).empty());
}

BOOST_AUTO_TEST_CASE(p2mr_descriptor_expand_and_sign)
{
    const CExtKey master{MasterKey(9)};
    FlatSigningProvider keys;
    std::string error;
    auto descs{Parse("p2mr(" + EncodeExtKey(master) + "/360h/0h/0h/0h/*h)", keys, error, false)};
    BOOST_REQUIRE_MESSAGE(descs.size() == 1, error);
    const Descriptor& desc{*descs[0]};

    // Expansion with the private key, then from the cache alone (what the wallet does).
    Expanded e0;
    BOOST_REQUIRE(desc.Expand(0, keys, e0.scripts, e0.out, &e0.cache));
    BOOST_REQUIRE_EQUAL(e0.scripts.size(), 1U);
    int version;
    std::vector<unsigned char> program;
    BOOST_REQUIRE(e0.scripts[0].IsWitnessProgram(version, program));
    BOOST_CHECK_EQUAL(version, 1);
    BOOST_CHECK_EQUAL(program.size(), 32U);
    BOOST_CHECK_EQUAL(e0.cache.GetCachedP2MRKeysAll().size(), 1U);

    Expanded from_cache;
    BOOST_REQUIRE(desc.ExpandFromCache(0, e0.cache, from_cache.scripts, from_cache.out));
    BOOST_CHECK(from_cache.scripts == e0.scripts);
    // Without the private key and without the cache, nothing can be derived.
    Expanded nothing;
    BOOST_CHECK(!desc.Expand(0, DUMMY_SIGNING_PROVIDER, nothing.scripts, nothing.out));
    // Other indexes give other outputs.
    Expanded e1;
    BOOST_REQUIRE(desc.Expand(1, keys, e1.scripts, e1.out, &e1.cache));
    BOOST_CHECK(e1.scripts != e0.scripts);

    // Signing: the solving data from the cache plus the private keys (ExpandPrivate).
    FlatSigningProvider signer{from_cache.out};
    desc.ExpandPrivate(0, keys, signer);
    BOOST_CHECK_EQUAL(signer.keys.size(), 2U); // the address key and the recovery key

    for (const bool recovery : {false, true}) {
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        SignOptions options;
        options.p2mr_recovery = recovery;
        BOOST_REQUIRE_MESSAGE(SignAndVerify(tx, prev, signer, options, sigdata), (recovery ? "recovery leaf" : "hybrid leaf"));
        BOOST_CHECK(sigdata.complete);
        BOOST_CHECK_EQUAL(int64_t(GetSerializeSize(tx.vin[0].scriptWitness.stack)),
                          recovery ? p2mr::RecoveryWitnessSize(false) : p2mr::HybridWitnessSize(false));

        // A changed signature byte fails.
        CMutableTransaction bad{tx};
        bad.vin[0].scriptWitness.stack[0][10] ^= 1;
        PrecomputedTransactionData txdata;
        txdata.Init(bad, {prev}, true);
        BOOST_CHECK(!VerifyScript(bad.vin[0].scriptSig, prev.scriptPubKey, &bad.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_P2MR,
                                  MutableTransactionSignatureChecker{&bad, 0, prev.nValue, txdata, MissingDataBehavior::FAIL}));
    }

    // Explicit sighash type: one more byte per signature.
    {
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        SignOptions options;
        options.sighash_type = SIGHASH_ALL;
        BOOST_REQUIRE(SignAndVerify(tx, prev, signer, options, sigdata));
        BOOST_CHECK_EQUAL(int64_t(GetSerializeSize(tx.vin[0].scriptWitness.stack)), p2mr::HybridWitnessSize(true));
    }

    // Without private keys the solving data still gives the size (fee estimation), not a signature.
    {
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        BOOST_CHECK(!SignAndVerify(tx, prev, from_cache.out, {}, sigdata));
        SignatureData dummy;
        BOOST_REQUIRE(ProduceSignature(from_cache.out, DUMMY_SIGNATURE_CREATOR, prev.scriptPubKey, dummy));
        BOOST_CHECK_EQUAL(int64_t(GetSerializeSize(dummy.scriptWitness.stack)), p2mr::HybridWitnessSize(false));
    }

    // Another descriptor's keys cannot sign this output.
    {
        FlatSigningProvider other_keys;
        auto other{Parse("p2mr(" + EncodeExtKey(MasterKey(10)) + "/360h/0h/0h/0h/*h)", other_keys, error, false)};
        FlatSigningProvider wrong{from_cache.out};
        other[0]->ExpandPrivate(0, other_keys, wrong);
        wrong.keys.erase(wrong.keys.begin(), wrong.keys.end());
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        BOOST_CHECK(!SignAndVerify(tx, prev, wrong, {}, sigdata));
    }
}

BOOST_AUTO_TEST_SUITE_END()
