// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/consensus.h>
#include <crypto/pqc/pqc.h>
#include <crypto/sha256.h>
#include <key.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <serialize.h>
#include <test/util/pqch.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

using namespace pqch_test;

/**
 * PQCH: post-quantum Lightning channels (PQIP 5), prototype L2. The channel outputs and their
 * spending paths, checked against the consensus script interpreter. No consensus change is needed:
 * these tests use only existing opcodes and flags.
 */
BOOST_FIXTURE_TEST_SUITE(pqch_lightning_tests, BasicTestingSetup)

namespace {

using Bytes = std::vector<unsigned char>;

/** One party's keys for one role: hybrid (Schnorr + ML-DSA-44) and its SLH-DSA twin (PQIP 5, sections 1-2). */
struct RoleKeys {
    CKey ec;
    pqc::KeyPair mldsa;
    pqc::KeyPair slh;

    explicit RoleKeys(uint8_t tag) : mldsa{MLDSAKey(tag)}, slh{SLHDSAKey(tag)}
    {
        std::array<unsigned char, 32> secret{};
        secret.fill(tag);
        ec.Set(secret.begin(), secret.end(), true);
    }
    Bytes XOnly() const
    {
        const XOnlyPubKey x{ec.GetPubKey()};
        return {x.begin(), x.end()};
    }
};

/** hybrid(k): <xonly> OP_CHECKSIGVERIFY <mldsa> OP_CHECKSIGMLDSA[VERIFY]. */
void Hybrid(CScript& s, const RoleKeys& k, bool verify)
{
    s << k.XOnly() << OP_CHECKSIGVERIFY << k.mldsa.pubkey << (verify ? OP_CHECKSIGMLDSAVERIFY : OP_CHECKSIGMLDSA);
}
/** slh(k): <slh> OP_CHECKSIGSLHDSA[VERIFY]. */
void Slh(CScript& s, const RoleKeys& k, bool verify)
{
    s << k.slh.pubkey << (verify ? OP_CHECKSIGSLHDSAVERIFY : OP_CHECKSIGSLHDSA);
}

/** Hash lock on a 32-byte secret: OP_SIZE 32 OP_EQUALVERIFY OP_SHA256 <H> OP_EQUALVERIFY. */
void HashLock(CScript& s, const Bytes& secret)
{
    uint256 h;
    CSHA256().Write(secret.data(), secret.size()).Finalize(h.begin());
    s << OP_SIZE << 32 << OP_EQUALVERIFY << OP_SHA256 << Bytes(h.begin(), h.end()) << OP_EQUALVERIFY;
}

/**
 * A leaf written once with "signers" (the parties whose signatures it needs, in script order); the
 * hybrid leaf and its SLH twin are built from the same description (PQIP 5, section 2).
 */
struct LeafSpec {
    CScript prefix;                       //!< conditions before the signatures (time locks, hash locks)
    std::vector<const RoleKeys*> signers; //!< in script order
};

CScript BuildLeaf(const LeafSpec& spec, bool slh)
{
    CScript s{spec.prefix};
    for (size_t i = 0; i < spec.signers.size(); ++i) {
        const bool last{i + 1 == spec.signers.size()};
        if (slh) Slh(s, *spec.signers[i], !last); else Hybrid(s, *spec.signers[i], !last);
    }
    return s;
}

/** A P2MR tree with any number of leaves (balanced, BIP341 tagged hashes). */
struct Tree {
    std::vector<CScript> leaves;
    std::vector<uint256> hashes;
    std::vector<std::vector<uint256>> paths;
    uint256 root;

    explicit Tree(std::vector<CScript> l) : leaves(std::move(l))
    {
        for (const auto& leaf : leaves) hashes.push_back(ComputeTapleafHash(P2MR_LEAF_PQSCRIPT, leaf));
        paths.resize(leaves.size());
        // Each node: its hash and the leaves below it.
        std::vector<std::pair<uint256, std::vector<size_t>>> level;
        for (size_t i = 0; i < hashes.size(); ++i) level.push_back({hashes[i], {i}});
        while (level.size() > 1) {
            std::vector<std::pair<uint256, std::vector<size_t>>> next;
            for (size_t i = 0; i + 1 < level.size(); i += 2) {
                for (size_t leaf : level[i].second) paths[leaf].push_back(level[i + 1].first);
                for (size_t leaf : level[i + 1].second) paths[leaf].push_back(level[i].first);
                auto below{level[i].second};
                below.insert(below.end(), level[i + 1].second.begin(), level[i + 1].second.end());
                next.push_back({ComputeTapbranchHash(level[i].first, level[i + 1].first), below});
            }
            if (level.size() % 2) next.push_back(level.back());
            level = std::move(next);
        }
        root = level[0].first;
    }
    CScript ScriptPubKey() const { return CScript() << OP_1 << Bytes(root.begin(), root.end()); }
    Bytes Control(size_t i) const
    {
        Bytes c{static_cast<unsigned char>(P2MR_LEAF_PQSCRIPT | 1)};
        for (const auto& h : paths[i]) c.insert(c.end(), h.begin(), h.end());
        return c;
    }
};

/** An output with leaves described once: leaf 2i is hybrid, leaf 2i+1 its SLH twin. */
struct ChannelOutput {
    std::vector<LeafSpec> specs;
    Tree tree;
    explicit ChannelOutput(std::vector<LeafSpec> s) : specs(std::move(s)), tree(Build(specs)) {}
    static Tree Build(const std::vector<LeafSpec>& specs)
    {
        std::vector<CScript> leaves;
        for (const auto& spec : specs) {
            leaves.push_back(BuildLeaf(spec, /*slh=*/false));
            leaves.push_back(BuildLeaf(spec, /*slh=*/true));
        }
        return Tree{std::move(leaves)};
    }
};

constexpr script_verify_flags FLAGS{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_P2MR |
                                    SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY};

/** Spending one channel output through leaf `leaf` with a one-input, one-output transaction. */
struct Spend {
    const ChannelOutput& out;
    CTxOut spent;
    CMutableTransaction tx;

    explicit Spend(const ChannelOutput& o, uint32_t sequence = CTxIn::SEQUENCE_FINAL, uint32_t locktime = 0) : out(o)
    {
        spent = CTxOut{10 * COIN, out.tree.ScriptPubKey()};
        tx.version = 3; // TRUC, as channel transactions (PQIP 5, section 4)
        tx.nLockTime = locktime;
        tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0}, CScript{}, sequence);
        tx.vout.emplace_back(10 * COIN - 50000, out.tree.ScriptPubKey());
    }
    uint256 Hash(size_t leaf, uint8_t key_version) const
    {
        return LeafSigHash(tx, 0, {spent}, out.tree.hashes.at(leaf), SIGHASH_DEFAULT, key_version);
    }
    /** Signatures of `signers` for leaf `leaf`, in witness order (the stack is consumed top first). */
    std::vector<Bytes> Sign(size_t leaf, const std::vector<const RoleKeys*>& signers) const
    {
        const bool slh{leaf % 2 == 1};
        std::vector<Bytes> items;
        for (auto it = signers.rbegin(); it != signers.rend(); ++it) {
            const RoleKeys& k{**it};
            if (slh) {
                items.push_back(SignSLHDSA(k.slh.seckey, Hash(leaf, static_cast<uint8_t>(PQAlgorithm::SLH_DSA_SHA2_128S))));
            } else {
                items.push_back(SignMLDSA(k.mldsa.seckey, Hash(leaf, static_cast<uint8_t>(PQAlgorithm::ML_DSA_44))));
                Bytes sig(64);
                BOOST_REQUIRE(k.ec.SignSchnorr(Hash(leaf, 0), sig, nullptr, uint256::ZERO));
                items.push_back(sig);
            }
        }
        return items;
    }
    void SetWitness(size_t leaf, std::vector<Bytes> items)
    {
        const auto& s{out.tree.leaves.at(leaf)};
        items.emplace_back(s.begin(), s.end());
        items.push_back(out.tree.Control(leaf));
        tx.vin[0].scriptWitness.stack = std::move(items);
    }
    ScriptError Verify(script_verify_flags flags = FLAGS) const
    {
        PrecomputedTransactionData txdata;
        txdata.Init(tx, std::vector<CTxOut>{spent});
        MutableTransactionSignatureChecker checker(&tx, 0, spent.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);
        ScriptError err;
        VerifyScript(tx.vin[0].scriptSig, spent.scriptPubKey, &tx.vin[0].scriptWitness, flags, checker, &err);
        return err;
    }
    size_t WitnessSize() const
    {
        size_t n{GetSizeOfCompactSize(tx.vin[0].scriptWitness.stack.size())};
        for (const auto& item : tx.vin[0].scriptWitness.stack) n += GetSizeOfCompactSize(item.size()) + item.size();
        return n;
    }
};

#define CHECK_ERR(expr, expected) BOOST_CHECK_EQUAL(ScriptErrorString(expr), ScriptErrorString(expected))

const RoleKeys& Key(uint8_t tag)
{
    static std::map<uint8_t, std::unique_ptr<RoleKeys>> cache;
    auto& k{cache[tag]};
    if (!k) k = std::make_unique<RoleKeys>(tag);
    return *k;
}

Bytes Secret(uint8_t tag) { return Bytes(32, tag); }

} // namespace

BOOST_AUTO_TEST_CASE(funding_output_2of2)
{
    const RoleKeys& a{Key(0x11)};
    const RoleKeys& b{Key(0x21)};
    const ChannelOutput funding{{{CScript{}, {&a, &b}}}}; // leaf 0 hybrid 2-of-2, leaf 1 SLH twin

    Spend s{funding};
    s.SetWitness(0, s.Sign(0, {&a, &b}));
    CHECK_ERR(s.Verify(), SCRIPT_ERR_OK);
    BOOST_TEST_MESSAGE("hybrid 2-of-2 witness: " << s.WitnessSize() << " bytes");
    BOOST_CHECK_LT(s.WitnessSize(), 8000U); // ~1.9 kvB (PQIP 5, security considerations)

    // One party alone cannot spend: B's signatures replaced by A's.
    s.SetWitness(0, s.Sign(0, {&a, &a}));
    BOOST_CHECK(s.Verify() != SCRIPT_ERR_OK);

    // The SLH twin works with both SLH-DSA signatures.
    Spend t{funding};
    t.SetWitness(1, t.Sign(1, {&a, &b}));
    CHECK_ERR(t.Verify(), SCRIPT_ERR_OK);
    BOOST_TEST_MESSAGE("SLH 2-of-2 witness: " << t.WitnessSize() << " bytes");
    BOOST_CHECK_LT(t.WitnessSize(), 16500U); // ~4 kvB
}

BOOST_AUTO_TEST_CASE(to_local_delay_and_revocation)
{
    const RoleKeys& local_delayed{Key(0x12)};
    const RoleKeys& remote_revocation{Key(0x22)};
    const Bytes per_commitment_secret{Secret(0x5c)};
    constexpr int64_t TO_SELF_DELAY{1440}; // PQIP 5, section 6

    CScript delay;
    delay << TO_SELF_DELAY << OP_CHECKSEQUENCEVERIFY << OP_DROP;
    CScript revocation;
    HashLock(revocation, per_commitment_secret);
    const ChannelOutput to_local{{{delay, {&local_delayed}}, {revocation, {&remote_revocation}}}};
    // leaves: 0 delayed (hybrid), 1 delayed (SLH), 2 revocation (hybrid), 3 revocation (SLH)

    // The broadcaster waits to_self_delay blocks.
    {
        Spend early{to_local, /*sequence=*/TO_SELF_DELAY - 1};
        early.SetWitness(0, early.Sign(0, {&local_delayed}));
        CHECK_ERR(early.Verify(), SCRIPT_ERR_UNSATISFIED_LOCKTIME);
        Spend ok{to_local, /*sequence=*/TO_SELF_DELAY};
        ok.SetWitness(0, ok.Sign(0, {&local_delayed}));
        CHECK_ERR(ok.Verify(), SCRIPT_ERR_OK);
    }
    // A revoked commitment: the other party takes everything at once with the revealed secret.
    {
        Spend penalty{to_local};
        auto w{penalty.Sign(2, {&remote_revocation})};
        w.push_back(per_commitment_secret); // consumed first, by the hash lock
        penalty.SetWitness(2, w);
        CHECK_ERR(penalty.Verify(), SCRIPT_ERR_OK);

        // The secret alone is useless to anyone else: a third party's key does not pass.
        Spend thief{to_local};
        auto wt{thief.Sign(2, {&Key(0x99)})};
        wt.push_back(per_commitment_secret);
        thief.SetWitness(2, wt);
        BOOST_CHECK(thief.Verify() != SCRIPT_ERR_OK);

        // Wrong secret, or the right one with a byte more: refused.
        for (const Bytes& bad : {Secret(0x5d), [&] { auto b{per_commitment_secret}; b.push_back(0); return b; }()}) {
            Spend s{to_local};
            auto w2{s.Sign(2, {&remote_revocation})};
            w2.push_back(bad);
            s.SetWitness(2, w2);
            CHECK_ERR(s.Verify(), SCRIPT_ERR_EQUALVERIFY);
        }
    }
}

BOOST_AUTO_TEST_CASE(htlc_paths)
{
    const RoleKeys& local_htlc{Key(0x13)};
    const RoleKeys& remote_htlc{Key(0x23)};
    const RoleKeys& remote_revocation{Key(0x22)};
    const Bytes preimage{Secret(0x77)};
    const Bytes per_commitment_secret{Secret(0x5c)};
    constexpr int64_t CLTV_EXPIRY{120'000};

    // Offered HTLC (from the broadcaster's side): the remote claims with the preimage; the local
    // side times out through a second-stage transaction signed by both; or revocation.
    CScript success;
    HashLock(success, preimage);
    CScript timeout; // second stage: both sign; the HTLC-timeout transaction carries nLockTime = expiry
    CScript revocation;
    HashLock(revocation, per_commitment_secret);
    const ChannelOutput offered{{{success, {&remote_htlc}}, {timeout, {&local_htlc, &remote_htlc}}, {revocation, {&remote_revocation}}}};

    Spend claim{offered};
    auto w{claim.Sign(0, {&remote_htlc})};
    w.push_back(preimage);
    claim.SetWitness(0, w);
    CHECK_ERR(claim.Verify(), SCRIPT_ERR_OK);

    Spend second_stage{offered, /*sequence=*/0, /*locktime=*/CLTV_EXPIRY};
    second_stage.SetWitness(2, second_stage.Sign(2, {&local_htlc, &remote_htlc}));
    CHECK_ERR(second_stage.Verify(), SCRIPT_ERR_OK);

    // Received HTLC: the remote side can take it back after the expiry (absolute time lock).
    CScript expiry;
    expiry << CLTV_EXPIRY << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    const ChannelOutput received{{{expiry, {&remote_htlc}}}};
    Spend too_early{received, /*sequence=*/0, /*locktime=*/CLTV_EXPIRY - 1};
    too_early.SetWitness(0, too_early.Sign(0, {&remote_htlc}));
    CHECK_ERR(too_early.Verify(), SCRIPT_ERR_UNSATISFIED_LOCKTIME);
    Spend refund{received, /*sequence=*/0, /*locktime=*/CLTV_EXPIRY};
    refund.SetWitness(0, refund.Sign(0, {&remote_htlc}));
    CHECK_ERR(refund.Verify(), SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(emergency_twins)
{
    // After the ML-DSA emergency soft fork (PQIP 4) the hybrid leaves are closed and every channel
    // output is still spendable, under the same conditions, through its SLH twin.
    const RoleKeys& local_delayed{Key(0x12)};
    CScript delay;
    delay << 1440 << OP_CHECKSEQUENCEVERIFY << OP_DROP;
    const ChannelOutput to_local{{{delay, {&local_delayed}}}};
    const script_verify_flags emergency{FLAGS | SCRIPT_VERIFY_MLDSA_DISABLED};

    Spend hybrid{to_local, 1440};
    hybrid.SetWitness(0, hybrid.Sign(0, {&local_delayed}));
    CHECK_ERR(hybrid.Verify(emergency), SCRIPT_ERR_MLDSA_DISABLED);

    Spend twin{to_local, 1440};
    twin.SetWitness(1, twin.Sign(1, {&local_delayed}));
    CHECK_ERR(twin.Verify(emergency), SCRIPT_ERR_OK);
    // The time lock still applies on the twin.
    Spend twin_early{to_local, 1439};
    twin_early.SetWitness(1, twin_early.Sign(1, {&local_delayed}));
    CHECK_ERR(twin_early.Verify(emergency), SCRIPT_ERR_UNSATISFIED_LOCKTIME);
}

BOOST_AUTO_TEST_CASE(message_and_policy_limits)
{
    // commitment_signed: one hybrid signature for the commitment plus one per HTLC, each 64 + 2420
    // bytes, must fit a 65,535-byte Lightning message (PQIP 5, section 5): at most 20 HTLCs.
    constexpr size_t HYBRID_SIG{64 + pqc::MLDSA44_SIG_SIZE};
    constexpr size_t OVERHEAD{32 + 2 + 2 + 64};               // channel id, counts, spare
    BOOST_CHECK_LE(OVERHEAD + HYBRID_SIG * (1 + 20), 65535U);
    BOOST_CHECK_GT(OVERHEAD + HYBRID_SIG * (1 + 26), 65535U);
}

BOOST_AUTO_TEST_SUITE_END()
