// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/p2mr.h>

#include <crypto/hmac_sha512.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <support/cleanse.h>
#include <sync.h>

#include <map>
#include <string_view>

namespace p2mr {
namespace {

/** HMAC-SHA512(key = label, data = secret [|| counter]); 64 bytes. */
void Expand(std::string_view label, const CKey& secret, unsigned char out[CHMAC_SHA512::OUTPUT_SIZE], uint8_t counter = 0)
{
    CHMAC_SHA512 h{reinterpret_cast<const unsigned char*>(label.data()), label.size()};
    h.Write(UCharCast(secret.data()), secret.size());
    if (counter) h.Write(&counter, 1);
    h.Finalize(out);
}

constexpr std::string_view LABEL_SCHNORR{"PQCH/P2MR/v1/schnorr"};
constexpr std::string_view LABEL_MLDSA{"PQCH/P2MR/v1/ml-dsa-44"};
constexpr std::string_view LABEL_SALT{"PQCH/P2MR/v1/salt"};
constexpr std::string_view LABEL_SLHDSA{"PQCH/P2MR/v1/slh-dsa-128s"};

Mutex g_recovery_mutex;
std::map<CPubKey, std::vector<unsigned char>> g_recovery_pubkeys GUARDED_BY(g_recovery_mutex);
constexpr size_t MAX_RECOVERY_MEMO{256};

} // namespace

bool DeriveLeafAKeys(const CKey& child, LeafAKeys& out)
{
    if (!child.IsValid()) return false;
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    // Schnorr key: the first 32 bytes, retried with a counter in the (negligible) case that they
    // are not a valid secp256k1 scalar.
    for (uint8_t counter = 0;; ++counter) {
        Expand(LABEL_SCHNORR, child, buf, counter);
        out.schnorr.Set(buf, buf + 32, /*fCompressedIn=*/true);
        if (out.schnorr.IsValid()) break;
        if (counter == 255) {
            memory_cleanse(buf, sizeof(buf));
            return false;
        }
    }
    Expand(LABEL_MLDSA, child, buf);
    const bool ok{pqc::MLDSA44KeyFromSeed(std::span<const unsigned char>{buf, pqc::KEY_SEED_SIZE}, out.mldsa)};
    Expand(LABEL_SALT, child, buf);
    std::copy(buf, buf + SALT_SIZE, out.salt.begin());
    memory_cleanse(buf, sizeof(buf));
    return ok;
}

bool DeriveRecoveryKey(const CKey& recovery_child, pqc::KeyPair& out)
{
    if (!recovery_child.IsValid()) return false;
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    Expand(LABEL_SLHDSA, recovery_child, buf);
    const bool ok{pqc::SLHDSA128sKeyFromSeed(std::span<const unsigned char>{buf, pqc::KEY_SEED_SIZE}, out)};
    memory_cleanse(buf, sizeof(buf));
    return ok;
}

bool DeriveRecoveryPubKey(const CKey& recovery_child, std::vector<unsigned char>& out)
{
    if (!recovery_child.IsValid()) return false;
    const CPubKey id{recovery_child.GetPubKey()};
    {
        LOCK(g_recovery_mutex);
        if (auto it{g_recovery_pubkeys.find(id)}; it != g_recovery_pubkeys.end()) {
            out = it->second;
            return true;
        }
    }
    pqc::KeyPair key;
    if (!DeriveRecoveryKey(recovery_child, key)) return false;
    out = key.pubkey;
    memory_cleanse(key.seckey.data(), key.seckey.size());
    LOCK(g_recovery_mutex);
    if (g_recovery_pubkeys.size() >= MAX_RECOVERY_MEMO) g_recovery_pubkeys.clear();
    g_recovery_pubkeys.emplace(id, out);
    return true;
}

bool DeriveAddressKeys(const CKey& child, const CKey& recovery_child, AddressKeys& out)
{
    LeafAKeys a;
    if (!DeriveLeafAKeys(child, a)) return false;
    if (!DeriveRecoveryPubKey(recovery_child, out.slhdsa)) return false;
    out.schnorr = XOnlyPubKey{a.schnorr.GetPubKey()};
    out.mldsa = a.mldsa.pubkey;
    out.salt = a.salt;
    out.recovery_owner = recovery_child.GetPubKey();
    memory_cleanse(a.mldsa.seckey.data(), a.mldsa.seckey.size());
    return out.IsValid();
}

CScript HybridLeaf(const XOnlyPubKey& schnorr, const std::vector<unsigned char>& mldsa)
{
    return CScript() << ToByteVector(schnorr) << OP_CHECKSIGVERIFY << mldsa << OP_CHECKSIGMLDSA;
}

CScript RecoveryLeaf(const std::array<unsigned char, SALT_SIZE>& salt, const std::vector<unsigned char>& slhdsa)
{
    return CScript() << std::vector<unsigned char>(salt.begin(), salt.end()) << OP_DROP << slhdsa << OP_CHECKSIGSLHDSA;
}

CScript SpendData::Leaf(bool recovery) const
{
    return recovery ? RecoveryLeaf(keys.salt, keys.slhdsa) : HybridLeaf(keys.schnorr, keys.mldsa);
}

uint256 SpendData::LeafHash(bool recovery) const
{
    return ComputeTapleafHash(P2MR_LEAF_PQSCRIPT, Leaf(recovery));
}

uint256 SpendData::Root() const
{
    return ComputeTapbranchHash(LeafHash(false), LeafHash(true));
}

std::vector<unsigned char> SpendData::Control(bool recovery) const
{
    std::vector<unsigned char> control{static_cast<unsigned char>(P2MR_LEAF_PQSCRIPT | 1)};
    const uint256 sibling{LeafHash(!recovery)};
    control.insert(control.end(), sibling.begin(), sibling.end());
    return control;
}

CScript SpendData::ScriptPubKey() const
{
    const uint256 root{Root()};
    return CScript() << OP_1 << std::vector<unsigned char>(root.begin(), root.end());
}

SpendData MakeSpendData(const AddressKeys& keys, const CPubKey& owner)
{
    return SpendData{.keys = keys, .owner = owner};
}

namespace {
int64_t Item(size_t size) { return GetSizeOfCompactSize(size) + size; }

SpendData SizeTemplate()
{
    AddressKeys k;
    k.mldsa.assign(pqc::MLDSA44_PUBKEY_SIZE, 0);
    k.slhdsa.assign(pqc::SLHDSA128S_PUBKEY_SIZE, 0);
    return MakeSpendData(k, {});
}
} // namespace

int64_t HybridWitnessSize(bool explicit_hashtype)
{
    const SpendData t{SizeTemplate()};
    const size_t extra{explicit_hashtype ? 1U : 0U};
    return GetSizeOfCompactSize(4) + Item(pqc::MLDSA44_SIG_SIZE + extra) + Item(64 + extra) + Item(t.Leaf(false).size()) + Item(t.Control(false).size());
}

int64_t RecoveryWitnessSize(bool explicit_hashtype)
{
    const SpendData t{SizeTemplate()};
    const size_t extra{explicit_hashtype ? 1U : 0U};
    return GetSizeOfCompactSize(3) + Item(pqc::SLHDSA128S_SIG_SIZE + extra) + Item(t.Leaf(true).size()) + Item(t.Control(true).size());
}

} // namespace p2mr
