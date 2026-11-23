// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: the NIST ACVP test vectors (FIPS 205) for SLH-DSA-SHA2-128s, the parameter set of the PQCH
// recovery key, run against the vendored slhdsa-c (src/crypto/pqc/PQCH-VERSION.md): key generation,
// signature generation (deterministic and with additional randomness, pure "external" interface with a
// context and the "internal" one) and signature verification, valid and modified signatures. The file
// holds only that parameter set and those interfaces (contrib/pqch/acvp_slhdsa.py made it).

#include <crypto/pqc/slhdsa-c/slh_dsa.h>
#include <test/data/slhdsa_sha2_128s_acvp.json.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

namespace {
std::vector<unsigned char> Hex(const UniValue& obj, const std::string& key)
{
    const UniValue& v{obj.find_value(key)};
    return v.isNull() ? std::vector<unsigned char>{} : ParseHex(v.get_str());
}

UniValue Vectors()
{
    UniValue v;
    Assert(v.read(json_tests::slhdsa_sha2_128s_acvp));
    return v;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(pqch_slhdsa_acvp_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(keygen)
{
    const UniValue tests{Vectors().find_value("keygen")};
    BOOST_REQUIRE_EQUAL(tests.size(), 10U);
    for (const UniValue& t : tests.getValues()) {
        const auto sk_seed{Hex(t, "skSeed")}, sk_prf{Hex(t, "skPrf")}, pk_seed{Hex(t, "pkSeed")};
        std::vector<unsigned char> sk(slh_sk_sz(&slh_dsa_sha2_128s)), pk(slh_pk_sz(&slh_dsa_sha2_128s));
        BOOST_REQUIRE_EQUAL(slh_keygen_internal(sk.data(), pk.data(), sk_seed.data(), sk_prf.data(), pk_seed.data(), &slh_dsa_sha2_128s), 0);
        BOOST_CHECK_MESSAGE(sk == Hex(t, "sk") && pk == Hex(t, "pk"), "keygen tcId " << t.find_value("tcId").getInt<int>());
    }
}

BOOST_AUTO_TEST_CASE(siggen)
{
    const UniValue tests{Vectors().find_value("siggen")};
    BOOST_REQUIRE_EQUAL(tests.size(), 28U);
    const size_t sig_sz{slh_sig_sz(&slh_dsa_sha2_128s)};
    for (const UniValue& t : tests.getValues()) {
        const auto sk{Hex(t, "sk")}, msg{Hex(t, "message")}, ctx{Hex(t, "context")}, addrnd{Hex(t, "additionalRandomness")};
        const bool deterministic{t.find_value("deterministic").get_bool()};
        BOOST_REQUIRE_EQUAL(deterministic, addrnd.empty());
        std::vector<unsigned char> sig(sig_sz);
        const size_t len{t.find_value("interface").get_str() == "internal"
                             ? slh_sign_internal(sig.data(), msg.data(), msg.size(), sk.data(), deterministic ? nullptr : addrnd.data(), &slh_dsa_sha2_128s)
                             : slh_sign(sig.data(), msg.data(), msg.size(), ctx.data(), ctx.size(), sk.data(), deterministic ? nullptr : addrnd.data(), &slh_dsa_sha2_128s)};
        BOOST_REQUIRE_EQUAL(len, sig_sz);
        BOOST_CHECK_MESSAGE(sig == Hex(t, "signature"), "siggen tgId " << t.find_value("tgId").getInt<int>() << " tcId " << t.find_value("tcId").getInt<int>());
    }
}

BOOST_AUTO_TEST_CASE(sigver)
{
    const UniValue tests{Vectors().find_value("sigver")};
    BOOST_REQUIRE_EQUAL(tests.size(), 28U);
    int valid{0}, invalid{0};
    for (const UniValue& t : tests.getValues()) {
        const auto pk{Hex(t, "pk")}, msg{Hex(t, "message")}, ctx{Hex(t, "context")}, sig{Hex(t, "signature")};
        const bool expected{t.find_value("testPassed").get_bool()};
        const int ok{t.find_value("interface").get_str() == "internal"
                         ? slh_verify_internal(msg.data(), msg.size(), sig.data(), sig.size(), pk.data(), &slh_dsa_sha2_128s)
                         : slh_verify(msg.data(), msg.size(), sig.data(), sig.size(), ctx.data(), ctx.size(), pk.data(), &slh_dsa_sha2_128s)};
        BOOST_CHECK_MESSAGE((ok == 1) == expected, "sigver tgId " << t.find_value("tgId").getInt<int>() << " tcId " << t.find_value("tcId").getInt<int>()
                                                                 << " (" << t.find_value("reason").get_str() << ")");
        (expected ? valid : invalid)++;
    }
    BOOST_CHECK_EQUAL(valid, 4);   // the valid case of each kind
    BOOST_CHECK_EQUAL(invalid, 24); // modified R, SIGFORS, SIGHT or message; too small; too large
}

BOOST_AUTO_TEST_SUITE_END()
