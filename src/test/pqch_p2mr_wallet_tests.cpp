// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/bip39.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/p2mr.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <boost/test/unit_test.hpp>

namespace {

using p2mr::HARDENED;

p2mr::PQSeed Seed(uint8_t tag)
{
    const std::vector<unsigned char> b(p2mr::SEED_SIZE, tag);
    return p2mr::PQSeed{b};
}

const p2mr::Path PATH0{360 | HARDENED, 0 | HARDENED, 0 | HARDENED, 0 | HARDENED, 0 | HARDENED};
const p2mr::Path PATH1{360 | HARDENED, 0 | HARDENED, 0 | HARDENED, 0 | HARDENED, 1 | HARDENED};
const p2mr::Path RECOVERY{360 | HARDENED, 0 | HARDENED, 0 | HARDENED, 0 | HARDENED, p2mr::RECOVERY_KEY_INDEX | HARDENED};

std::string PrivDesc(const p2mr::PQSeed& seed) { return "p2mr(pqprv(" + HexStr(seed.bytes) + ")/360h/0h/0h/0h/*h)"; }

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
    const p2mr::PQSeed seed{Seed(1)};
    p2mr::AddressKeys a, b;
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(seed, PATH0, RECOVERY, a));
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(seed, PATH0, RECOVERY, b));
    BOOST_CHECK(a == b); // deterministic: the seed recovers every key
    BOOST_CHECK(a.IsValid());

    p2mr::LeafAKeys leaf_a;
    BOOST_REQUIRE(p2mr::DeriveLeafAKeys(seed, PATH0, leaf_a));
    BOOST_CHECK(XOnlyPubKey{leaf_a.schnorr.GetPubKey()} == a.schnorr);
    BOOST_CHECK(leaf_a.mldsa.pubkey == a.mldsa);

    // Another address: other keys and salt, same recovery key.
    p2mr::AddressKeys c;
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(seed, PATH1, RECOVERY, c));
    BOOST_CHECK(c.schnorr != a.schnorr);
    BOOST_CHECK(c.mldsa != a.mldsa);
    BOOST_CHECK(c.salt != a.salt);
    BOOST_CHECK(c.slhdsa == a.slhdsa);
    const p2mr::SpendData sa{.keys = a}, sc{.keys = c};
    // The recovery leaf hash (revealed when leaf A is spent) differs between addresses.
    BOOST_CHECK(sa.LeafHash(true) != sc.LeafHash(true));
    BOOST_CHECK(sa.Root() != sc.Root());
    // Another seed, other keys.
    p2mr::AddressKeys d;
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(Seed(2), PATH0, RECOVERY, d));
    BOOST_CHECK(d.mldsa != a.mldsa && d.slhdsa != a.slhdsa);

    // The memoised recovery public key equals a fresh derivation.
    pqc::KeyPair slh;
    BOOST_REQUIRE(p2mr::DeriveRecoveryKey(seed, RECOVERY, slh));
    BOOST_CHECK(slh.pubkey == a.slhdsa);

    // Seed id: a tagged hash, never the seed.
    BOOST_CHECK(seed.Id() != uint256{std::span<const unsigned char>{seed.bytes}});
    BOOST_CHECK(seed.Id() != Seed(2).Id());
    // A new wallet's seed comes from its random master key material, deterministically.
    CExtKey master;
    master.SetSeed(std::vector<std::byte>(32, std::byte{5}));
    BOOST_CHECK(p2mr::SeedFromMasterKey(master).bytes == p2mr::SeedFromMasterKey(master).bytes);

    // Witness sizes of the two paths.
    BOOST_CHECK_EQUAL(p2mr::HybridWitnessSize(false), 3876);
    BOOST_CHECK_EQUAL(p2mr::RecoveryWitnessSize(false), 7947);

    // Fixed vector, so a change to the derivation is noticed (it would lose users' funds). The id,
    // Schnorr key and salt were checked against an independent Python implementation (CARTEIRA.md).
    BOOST_TEST_MESSAGE("P2MR v2 vector: id " << HexStr(seed.Id()) << " schnorr " << HexStr(a.schnorr) << " salt " << HexStr(a.salt) << " root " << sa.Root().GetHex());
    BOOST_CHECK_EQUAL(HexStr(seed.Id()), "2637734506e0947725c149ec43ee8251eed354afb36130e824b2fa6e7121180e");
    BOOST_CHECK_EQUAL(HexStr(a.schnorr), "bcf67cf39ef75c31a813008dd8834a798f68464a9185ab27c35344d9209a9d8f");
    BOOST_CHECK_EQUAL(HexStr(a.salt), "4bb7196e788b8026f67e4bc5c1b6afb7");
    BOOST_CHECK_EQUAL(sa.Root().GetHex(), "213f422e9312793b15c051f98aa8501a313bb6fe5ab52881a95006bcf043ec52");
}

BOOST_AUTO_TEST_CASE(bip39_recovery_words)
{
    // Official BIP39 vectors (Trezor, passphrase "TREZOR"); the PQCH seeds were computed with an
    // independent Python implementation (hashlib PBKDF2 and HMAC).
    struct Vector { std::string entropy, words, passphrase, bip39_seed, pq_seed; };
    const Vector vectors[]{
        {std::string(64, '0'),
         "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art",
         "TREZOR",
         "bda85446c68413707090a52022edd26a1c9462295029f2e60cd7c4f2bbd3097170af7a4d73245cafa9c3cca8d561a7c3de6f5d4a10be8ed2a5e608d68f92fcc8",
         "a855eaa0d7cdcb440a9f75d0560f6f268fc175938903fcaa2dfdf399962496a7"},
        {"7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f",
         "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth title",
         "TREZOR",
         "bc09fca1804f7e69da93c2f2028eb238c227f2e9dda30cd63699232578480a4021b146ad717fbb7e451ce9eb835f43620bf5c514db0f8add49f5d121449d3e87",
         "d14480330700192ce3e6c1ba480a7c0333eb8b80e0fe1036ca2c1a2c63d7ec65"},
        {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
         "abandon amount liar amount expire adjust cage candy arch gather drum bullet absurd math era live bid rhythm alien crouch range attend journey unaware",
         "",
         "0a6d060f6242aece4b074e48e7d8166f792a9b2bb7b295fa5ac289eda7647290c3d80e7436d6e9e34e72769c06f6582192d0b57ae4a97e9e24c8972a770a57d9",
         "f336e7e66cf493dde725edbc904755a3e1c43a24e9719ee5b2fec1a5455970c1"},
    };
    for (const auto& v : vectors) {
        const auto entropy{ParseHex(v.entropy)};
        BOOST_CHECK_EQUAL(bip39::MnemonicFromEntropy(std::span<const unsigned char, 32>{entropy.data(), 32}), v.words);
        const auto back{bip39::EntropyFromMnemonic(v.words)};
        BOOST_REQUIRE(back);
        BOOST_CHECK_EQUAL(HexStr(*back), v.entropy);
        const auto seed{bip39::SeedFromMnemonic(v.words, v.passphrase)};
        BOOST_REQUIRE(seed);
        BOOST_CHECK_EQUAL(HexStr(*seed), v.bip39_seed);
        const auto pq{p2mr::SeedFromMnemonic(v.words, v.passphrase)};
        BOOST_REQUIRE(pq);
        BOOST_CHECK_EQUAL(HexStr(pq->bytes), v.pq_seed);
    }
    const std::string ok{vectors[0].words};
    // Extra whitespace is fine; the canonical form has single spaces.
    BOOST_CHECK_EQUAL(*bip39::NormalizeMnemonic("  " + ok + " \n"), ok);
    // Wrong checksum, unknown word, wrong count, upper case, non-ASCII passphrase: refused.
    BOOST_CHECK(!bip39::EntropyFromMnemonic(ok.substr(0, ok.size() - 3) + "ability"));
    BOOST_CHECK(!bip39::EntropyFromMnemonic(ok.substr(0, ok.size() - 3) + "pqchx"));
    BOOST_CHECK(!bip39::EntropyFromMnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"));
    BOOST_CHECK(!bip39::EntropyFromMnemonic("Abandon" + ok.substr(7)));
    BOOST_CHECK(!p2mr::SeedFromMnemonic(ok, "pa\xc3\xa7o"));
    // Another passphrase gives another wallet.
    BOOST_CHECK(p2mr::SeedFromMnemonic(ok, "")->bytes != p2mr::SeedFromMnemonic(ok, "TREZOR")->bytes);
}

BOOST_AUTO_TEST_CASE(p2mr_descriptor_parse)
{
    const p2mr::PQSeed seed{Seed(7)};
    FlatSigningProvider keys;
    std::string error;

    auto priv{Parse(PrivDesc(seed), keys, error, /*require_checksum=*/false)};
    BOOST_REQUIRE_MESSAGE(priv.size() == 1, error);
    BOOST_CHECK(priv[0]->IsRange());
    BOOST_CHECK(priv[0]->GetOutputType() == OutputType::BECH32M);
    BOOST_CHECK(keys.pq_seeds.contains(seed.Id()));
    // The public form shows only the seed's id; the private form gives the seed back.
    const std::string pub{"p2mr(pqseed(" + HexStr(seed.Id()) + ")/360h/0h/0h/0h/*h)"};
    BOOST_CHECK(priv[0]->ToString().starts_with(pub + "#"));
    std::string priv_str;
    BOOST_REQUIRE(priv[0]->ToPrivateString(keys, priv_str));
    BOOST_CHECK(priv_str.starts_with(PrivDesc(seed) + "#"));
    BOOST_CHECK(!priv[0]->ToPrivateString(DUMMY_SIGNING_PROVIDER, priv_str));
    BOOST_CHECK(priv[0]->HavePrivateKeys(keys));
    // The public string parses to the same descriptor, without the seed.
    FlatSigningProvider none;
    auto pubd{Parse(pub, none, error, false)};
    BOOST_REQUIRE(pubd.size() == 1);
    BOOST_CHECK_EQUAL(pubd[0]->ToString(), priv[0]->ToString());
    BOOST_CHECK(none.pq_seeds.empty());
    BOOST_CHECK_EQUAL(*priv[0]->MaxSatisfactionWeight(false), p2mr::HybridWitnessSize(true) - 1); // without the element count

    // Bad forms: elliptic curve keys, unhardened steps, * not last, wrong sizes, not top level.
    CExtKey master;
    master.SetSeed(std::vector<std::byte>(32, std::byte{5}));
    for (const std::string& bad : {
             "p2mr(" + EncodeExtKey(master) + "/360h/*h)",
             "p2mr(pqseed(" + HexStr(seed.Id()) + ")/360/*h)",
             "p2mr(pqseed(" + HexStr(seed.Id()) + ")/360h/*)",
             "p2mr(pqseed(" + HexStr(seed.Id()) + ")/*h/0h)",
             std::string{"p2mr(pqseed(00)/0h)"},
             "sh(p2mr(pqseed(" + HexStr(seed.Id()) + ")/0h))"}) {
        FlatSigningProvider k;
        BOOST_CHECK_MESSAGE(Parse(bad, k, error, false).empty(), bad);
    }
}

BOOST_AUTO_TEST_CASE(p2mr_descriptor_expand_and_sign)
{
    const p2mr::PQSeed seed{Seed(9)};
    FlatSigningProvider keys;
    std::string error;
    auto descs{Parse(PrivDesc(seed), keys, error, false)};
    BOOST_REQUIRE_MESSAGE(descs.size() == 1, error);
    const Descriptor& desc{*descs[0]};

    // Expansion with the seed, then from the cache alone (what the wallet does).
    Expanded e0;
    BOOST_REQUIRE(desc.Expand(0, keys, e0.scripts, e0.out, &e0.cache));
    BOOST_REQUIRE_EQUAL(e0.scripts.size(), 1U);
    int version;
    std::vector<unsigned char> program;
    BOOST_REQUIRE(e0.scripts[0].IsWitnessProgram(version, program));
    BOOST_CHECK_EQUAL(version, 1);
    BOOST_CHECK_EQUAL(program.size(), 32U);
    BOOST_CHECK_EQUAL(e0.cache.GetCachedP2MRKeysAll().size(), 1U);
    // No elliptic curve key of the derivation anywhere in the cache or the solving data.
    BOOST_CHECK(e0.cache.GetCachedDerivedExtPubKeys().empty() && e0.cache.GetCachedParentExtPubKeys().empty());
    BOOST_CHECK(e0.out.pubkeys.empty() && e0.out.keys.empty() && e0.out.pq_seeds.empty());

    Expanded from_cache;
    BOOST_REQUIRE(desc.ExpandFromCache(0, e0.cache, from_cache.scripts, from_cache.out));
    BOOST_CHECK(from_cache.scripts == e0.scripts);
    // Without the seed and without the cache, nothing can be derived.
    Expanded nothing;
    BOOST_CHECK(!desc.Expand(0, DUMMY_SIGNING_PROVIDER, nothing.scripts, nothing.out));
    // The recovery key's position is not an address.
    Expanded reserved;
    BOOST_CHECK(!desc.Expand(p2mr::RECOVERY_KEY_INDEX, keys, reserved.scripts, reserved.out));
    // Other indexes give other outputs; same as the library derivation.
    Expanded e1;
    BOOST_REQUIRE(desc.Expand(1, keys, e1.scripts, e1.out, &e1.cache));
    BOOST_CHECK(e1.scripts != e0.scripts);
    p2mr::SpendData lib{.path = PATH1, .recovery_path = RECOVERY};
    BOOST_REQUIRE(p2mr::DeriveAddressKeys(seed, PATH1, RECOVERY, lib.keys));
    BOOST_CHECK(e1.scripts[0] == lib.ScriptPubKey());

    // A fixed-index descriptor is the same address as that index of the ranged one.
    {
        FlatSigningProvider k2;
        auto fixed{Parse("p2mr(pqprv(" + HexStr(seed.bytes) + ")/360h/0h/0h/0h/1h)", k2, error, false)};
        BOOST_REQUIRE(fixed.size() == 1);
        Expanded f;
        BOOST_REQUIRE(fixed[0]->Expand(0, k2, f.scripts, f.out));
        BOOST_CHECK(f.scripts == e1.scripts);
        BOOST_CHECK(Parse("p2mr(pqprv(" + HexStr(seed.bytes) + ")/360h/2147483647h)", k2, error, false).empty());
    }

    // Signing: the solving data from the cache plus the seed (ExpandPrivate).
    FlatSigningProvider signer{from_cache.out};
    desc.ExpandPrivate(0, keys, signer);
    BOOST_CHECK_EQUAL(signer.pq_seeds.size(), 1U);
    BOOST_CHECK(signer.keys.empty());

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

    // Without the seed the solving data still gives the size (fee estimation), not a signature.
    {
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        BOOST_CHECK(!SignAndVerify(tx, prev, from_cache.out, {}, sigdata));
        SignatureData dummy;
        BOOST_REQUIRE(ProduceSignature(from_cache.out, DUMMY_SIGNATURE_CREATOR, prev.scriptPubKey, dummy));
        BOOST_CHECK_EQUAL(int64_t(GetSerializeSize(dummy.scriptWitness.stack)), p2mr::HybridWitnessSize(false));
    }

    // Another seed cannot sign this output.
    {
        FlatSigningProvider wrong{from_cache.out};
        wrong.pq_seeds.emplace(seed.Id(), Seed(10)); // the wrong seed under this id
        CTxOut prev;
        CMutableTransaction tx{Spend(e0.scripts[0], prev)};
        SignatureData sigdata;
        BOOST_CHECK(!SignAndVerify(tx, prev, wrong, {}, sigdata));
    }
}

BOOST_AUTO_TEST_CASE(p2mr_vault_descriptor)
{
    // The owner's recovery key (getvaultkey) and an agent's vault seed (createvault).
    const p2mr::PQSeed owner{Seed(21)}, agent{Seed(22)};
    const p2mr::PQSeed vault_seed{p2mr::VaultSeed(agent, 0)};
    BOOST_CHECK(vault_seed.Id() != agent.Id());
    BOOST_CHECK(p2mr::VaultSeed(agent, 1).Id() != vault_seed.Id());
    const p2mr::Path key_path{p2mr::VaultKeyPath(/*test_chain=*/true, 0)};
    std::vector<unsigned char> owner_pub;
    BOOST_REQUIRE(p2mr::DeriveRecoveryPubKey(owner, key_path, owner_pub));
    const std::string slh{"slh([" + HexStr(owner.Id()) + "/360h/1h/3h/0h]" + HexStr(owner_pub) + ")"};
    const auto vault_desc{[&](const std::string& seed, uint32_t older) {
        return "p2mr_vault(" + seed + "/360h/1h/2h/0h/*h," + slh + ",older(" + util::ToString(older) + "))";
    }};
    const std::string priv{vault_desc("pqprv(" + HexStr(vault_seed.bytes) + ")", 144)};

    FlatSigningProvider keys;
    std::string error;
    auto descs{Parse(priv, keys, error, false)};
    BOOST_REQUIRE_MESSAGE(descs.size() == 1, error);
    const Descriptor& desc{*descs[0]};
    BOOST_CHECK(desc.IsRange() && desc.IsSolvable());
    BOOST_CHECK(desc.GetOutputType() == OutputType::BECH32M);
    BOOST_REQUIRE(desc.GetP2MRVault());
    BOOST_CHECK(desc.GetP2MRVault()->first == owner.Id());
    BOOST_CHECK_EQUAL(desc.GetP2MRVault()->second, 144U);
    // Public form: the vault seed's id, never a seed; it parses back to the same descriptor.
    const std::string pub{desc.ToString()};
    BOOST_CHECK(pub.starts_with(vault_desc("pqseed(" + HexStr(vault_seed.Id()) + ")", 144) + "#"));
    std::string priv_out;
    BOOST_REQUIRE(desc.ToPrivateString(keys, priv_out));
    BOOST_CHECK(priv_out.starts_with(priv + "#"));
    BOOST_CHECK(priv_out.find(HexStr(owner.bytes)) == std::string::npos);
    FlatSigningProvider none;
    BOOST_CHECK_EQUAL(Parse(pub, none, error, true).size(), 1U);

    // Bad forms.
    for (const std::string& bad : std::vector<std::string>{
             vault_desc("pqprv(" + HexStr(vault_seed.bytes) + ")", p2mr::MAX_VAULT_OLDER + 1),
             "p2mr_vault(pqprv(" + HexStr(vault_seed.bytes) + ")/360h/1h/2h/0h/*h," + slh + ")",
             "p2mr_vault(pqprv(" + HexStr(vault_seed.bytes) + ")/360h/1h/2h/0h/*h,slh(00),older(1))",
             "p2mr_vault(pqprv(" + HexStr(vault_seed.bytes) + ")/360h/1h/2/0h/*h," + slh + ",older(1))",
             "p2mr_vault(pqprv(" + HexStr(vault_seed.bytes) + ")/360h/1h/2h/0h/*h,slh([" + HexStr(owner.Id()) + "/*h]" + HexStr(owner_pub) + "),older(1))",
             "p2mr_vault(pqprv(" + HexStr(vault_seed.bytes) + ")/360h/1h/2h/0h/*h," + slh + ",after(1))",
             "sh(" + priv + ")",
         }) {
        FlatSigningProvider k;
        BOOST_CHECK_MESSAGE(Parse(bad, k, error, false).empty(), bad);
    }

    // Leaf A is the vault seed's hybrid leaf; leaf B the owner's key behind the timelock.
    Expanded e;
    BOOST_REQUIRE(desc.Expand(0, keys, e.scripts, e.out, &e.cache));
    BOOST_REQUIRE_EQUAL(e.out.p2mr_spenddata.size(), 1U);
    const p2mr::SpendData spend{e.out.p2mr_spenddata.begin()->second};
    BOOST_CHECK(spend.keys.slhdsa == owner_pub);
    BOOST_CHECK(spend.RecoverySeedId() == owner.Id());
    BOOST_CHECK(spend.Leaf(true) == p2mr::RecoveryLeaf(spend.keys.salt, owner_pub, 144));
    BOOST_CHECK(spend.Leaf(true) != p2mr::RecoveryLeaf(spend.keys.salt, owner_pub));
    Expanded c;
    BOOST_REQUIRE(desc.ExpandFromCache(0, e.cache, c.scripts, c.out));
    BOOST_CHECK(c.scripts == e.scripts);
    // older(0): leaf B without the timelock, another address.
    {
        FlatSigningProvider k0;
        auto d0{Parse(vault_desc("pqprv(" + HexStr(vault_seed.bytes) + ")", 0), k0, error, false)};
        BOOST_REQUIRE(d0.size() == 1);
        Expanded e0;
        BOOST_REQUIRE(d0[0]->Expand(0, k0, e0.scripts, e0.out));
        BOOST_CHECK(e0.scripts != e.scripts);
        BOOST_CHECK(e0.out.p2mr_spenddata.begin()->second.Leaf(true) == p2mr::RecoveryLeaf(spend.keys.salt, owner_pub));
    }

    // The agent (vault seed only) signs leaf A, not leaf B; the owner (with its seed) signs leaf B
    // once the input's nSequence meets the timelock.
    FlatSigningProvider agent_signer{c.out};
    desc.ExpandPrivate(0, keys, agent_signer);
    FlatSigningProvider owner_signer{agent_signer};
    owner_signer.pq_seeds.emplace(owner.Id(), owner);
    const auto try_sign{[&](const FlatSigningProvider& signer, bool recovery, uint32_t sequence) {
        CTxOut prev;
        CMutableTransaction tx{Spend(e.scripts[0], prev)};
        tx.vin[0].nSequence = sequence;
        SignatureData sigdata;
        SignOptions options;
        options.p2mr_recovery = recovery;
        const bool ok{SignAndVerify(tx, prev, signer, options, sigdata)};
        if (ok) {
            BOOST_CHECK_EQUAL(int64_t(GetSerializeSize(tx.vin[0].scriptWitness.stack)),
                              recovery ? p2mr::RecoveryWitnessSize(false, 144) : p2mr::HybridWitnessSize(false));
        }
        return ok;
    }};
    BOOST_CHECK(try_sign(agent_signer, /*recovery=*/false, CTxIn::SEQUENCE_FINAL));
    BOOST_CHECK(!try_sign(agent_signer, /*recovery=*/true, 144));
    BOOST_CHECK(try_sign(owner_signer, /*recovery=*/true, 144));
    BOOST_CHECK(try_sign(owner_signer, /*recovery=*/true, 1000));
    BOOST_CHECK(!try_sign(owner_signer, /*recovery=*/true, 143));
    BOOST_CHECK(!try_sign(owner_signer, /*recovery=*/true, CTxIn::MAX_SEQUENCE_NONFINAL));
    // A wrong owner key in the descriptor: the owner's seed does not sign for it.
    {
        std::vector<unsigned char> other_pub;
        BOOST_REQUIRE(p2mr::DeriveRecoveryPubKey(owner, p2mr::VaultKeyPath(true, 1), other_pub));
        BOOST_CHECK(other_pub != owner_pub);
    }
}

BOOST_AUTO_TEST_SUITE_END()
