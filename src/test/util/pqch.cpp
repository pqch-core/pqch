// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/pqch.h>

#include <bech32.h>
#include <chainparams.h>
#include <script/interpreter.h>
#include <util/check.h>

#include <algorithm>
#include <array>

namespace pqch_test {

CScript LeafMLDSA(const std::vector<unsigned char>& mldsa_pubkey)
{
    return CScript() << mldsa_pubkey << OP_CHECKSIGMLDSA;
}

CScript LeafHybrid(const std::vector<unsigned char>& xonly_pubkey, const std::vector<unsigned char>& mldsa_pubkey)
{
    return CScript() << xonly_pubkey << OP_CHECKSIGVERIFY << mldsa_pubkey << OP_CHECKSIGMLDSA;
}

CScript LeafSLHDSA(const std::vector<unsigned char>& slhdsa_pubkey)
{
    return CScript() << slhdsa_pubkey << OP_CHECKSIGSLHDSA;
}

uint256 P2MRTree::LeafHash(size_t i) const
{
    return ComputeTapleafHash(versions.at(i), leaves.at(i));
}

uint256 P2MRTree::Root() const
{
    Assert(leaves.size() == 1 || leaves.size() == 2);
    if (leaves.size() == 1) return LeafHash(0);
    return ComputeTapbranchHash(LeafHash(0), LeafHash(1));
}

std::vector<unsigned char> P2MRTree::Control(size_t i) const
{
    std::vector<unsigned char> control{static_cast<unsigned char>(versions.at(i) | 1)};
    if (leaves.size() == 2) {
        const uint256 sibling{LeafHash(1 - i)};
        control.insert(control.end(), sibling.begin(), sibling.end());
    }
    return control;
}

CScript P2MRTree::ScriptPubKey() const
{
    const uint256 root{Root()};
    return CScript() << OP_1 << std::vector<unsigned char>(root.begin(), root.end());
}

P2MRTree MakeTree(std::vector<CScript> leaves, uint8_t leaf_version)
{
    P2MRTree tree;
    tree.versions.assign(leaves.size(), leaf_version);
    tree.leaves = std::move(leaves);
    return tree;
}

uint256 LeafSigHash(const CMutableTransaction& tx, unsigned int in, const std::vector<CTxOut>& spent_outputs,
                    const uint256& leaf_hash, uint8_t hash_type, uint8_t key_version)
{
    PrecomputedTransactionData txdata;
    txdata.Init(tx, std::vector<CTxOut>(spent_outputs), /*force=*/true);
    ScriptExecutionData execdata;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false;
    execdata.m_tapleaf_hash_init = true;
    execdata.m_tapleaf_hash = leaf_hash;
    execdata.m_codeseparator_pos_init = true;
    execdata.m_codeseparator_pos = 0xFFFFFFFF;
    uint256 sighash;
    Assert(SignatureHashSchnorr(sighash, execdata, tx, in, hash_type, SigVersion::TAPSCRIPT, txdata, MissingDataBehavior::FAIL, key_version));
    return sighash;
}

std::vector<unsigned char> SignMLDSA(const std::vector<unsigned char>& seckey, const uint256& sighash, uint8_t hash_type)
{
    static constexpr std::array<unsigned char, 32> ZERO_RND{};
    std::vector<unsigned char> sig;
    Assert(pqc::MLDSA44Sign(seckey, sighash, pqc::TX_SIGNATURE_CONTEXT, ZERO_RND, sig));
    if (hash_type != SIGHASH_DEFAULT) sig.push_back(hash_type);
    return sig;
}

std::vector<unsigned char> SignSLHDSA(const std::vector<unsigned char>& seckey, const uint256& sighash, uint8_t hash_type)
{
    std::vector<unsigned char> sig;
    Assert(pqc::SLHDSA128sSign(seckey, sighash, pqc::TX_SIGNATURE_CONTEXT, {}, sig));
    if (hash_type != SIGHASH_DEFAULT) sig.push_back(hash_type);
    return sig;
}

std::string ToChainBech32Address(const std::string& bitcoin_address)
{
    const auto dec{bech32::Decode(bitcoin_address)};
    Assert(dec.encoding != bech32::Encoding::INVALID);
    return bech32::Encode(dec.encoding, Params().Bech32HRP(), dec.data);
}

pqc::KeyPair MLDSAKey(uint8_t tag)
{
    std::array<unsigned char, pqc::KEY_SEED_SIZE> seed{};
    seed.fill(tag);
    pqc::KeyPair key;
    Assert(pqc::MLDSA44KeyFromSeed(seed, key));
    return key;
}

pqc::KeyPair SLHDSAKey(uint8_t tag)
{
    std::array<unsigned char, pqc::KEY_SEED_SIZE> seed{};
    seed.fill(tag);
    pqc::KeyPair key;
    Assert(pqc::SLHDSA128sKeyFromSeed(seed, key));
    return key;
}

} // namespace pqch_test
