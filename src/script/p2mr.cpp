// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/p2mr.h>

#include <crypto/hmac_sha512.h>
#include <script/bip39.h>
#include <hash.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <sync.h>

#include <map>
#include <string_view>

namespace p2mr {
namespace {

constexpr std::string_view LABEL_NODE{"PQCH/P2MR/v2/node"};
constexpr std::string_view LABEL_SCHNORR{"PQCH/P2MR/v2/schnorr"};
constexpr std::string_view LABEL_MLDSA{"PQCH/P2MR/v2/ml-dsa-44"};
constexpr std::string_view LABEL_SALT{"PQCH/P2MR/v2/salt"};
constexpr std::string_view LABEL_SLHDSA{"PQCH/P2MR/v2/slh-dsa-128s"};
constexpr std::string_view LABEL_WALLET_SEED{"PQCH/P2MR/v2/wallet-seed"};

const HashWriter HASHER_SEED_ID{TaggedHash("PQCH/P2MR/v2/seed-id")};

/** A 32-byte secret, wiped when it goes away. */
struct Secret32 {
    std::array<unsigned char, 32> b{};
    ~Secret32() { memory_cleanse(b.data(), b.size()); }
};

void Node(const PQSeed& seed, const Path& path, Secret32& out)
{
    CHMAC_SHA512 h{seed.bytes.data(), seed.bytes.size()};
    h.Write(reinterpret_cast<const unsigned char*>(LABEL_NODE.data()), LABEL_NODE.size());
    for (uint32_t step : path) {
        const unsigned char be[4]{static_cast<unsigned char>(step >> 24), static_cast<unsigned char>(step >> 16),
                                  static_cast<unsigned char>(step >> 8), static_cast<unsigned char>(step)};
        h.Write(be, 4);
    }
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    h.Finalize(buf);
    std::copy(buf, buf + 32, out.b.begin());
    memory_cleanse(buf, sizeof(buf));
}

/** HMAC-SHA512(key = label, data = secret [|| counter]); 64 bytes. */
void Expand(std::string_view label, std::span<const unsigned char> secret, unsigned char out[CHMAC_SHA512::OUTPUT_SIZE], uint8_t counter = 0)
{
    CHMAC_SHA512 h{reinterpret_cast<const unsigned char*>(label.data()), label.size()};
    h.Write(secret.data(), secret.size());
    if (counter) h.Write(&counter, 1);
    h.Finalize(out);
}

Mutex g_recovery_mutex;
std::map<uint256, std::vector<unsigned char>> g_recovery_pubkeys GUARDED_BY(g_recovery_mutex); //!< by hash of the recovery node
constexpr size_t MAX_RECOVERY_MEMO{256};

} // namespace

PQSeed::PQSeed(std::span<const unsigned char> b)
{
    assert(b.size() == SEED_SIZE);
    std::copy(b.begin(), b.end(), bytes.begin());
}

uint256 PQSeed::Id() const
{
    HashWriter h{HASHER_SEED_ID};
    h.write(std::as_bytes(std::span{bytes}));
    return h.GetSHA256();
}

PQSeed SeedFromMasterKey(const CExtKey& master)
{
    CHMAC_SHA512 h{reinterpret_cast<const unsigned char*>(LABEL_WALLET_SEED.data()), LABEL_WALLET_SEED.size()};
    h.Write(UCharCast(master.key.data()), master.key.size());
    h.Write(master.chaincode.begin(), master.chaincode.size());
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    h.Finalize(buf);
    PQSeed seed{std::span<const unsigned char>{buf, SEED_SIZE}};
    memory_cleanse(buf, sizeof(buf));
    return seed;
}

std::optional<PQSeed> SeedFromMnemonic(std::string_view mnemonic, std::string_view passphrase)
{
    auto bip39_seed{bip39::SeedFromMnemonic(mnemonic, passphrase)};
    if (!bip39_seed) return std::nullopt;
    static constexpr std::string_view LABEL{"PQCH/P2MR/v2/bip39"};
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    CHMAC_SHA512{reinterpret_cast<const unsigned char*>(LABEL.data()), LABEL.size()}.Write(bip39_seed->data(), bip39_seed->size()).Finalize(buf);
    PQSeed seed{std::span<const unsigned char>{buf, SEED_SIZE}};
    memory_cleanse(buf, sizeof(buf));
    memory_cleanse(bip39_seed->data(), bip39_seed->size());
    return seed;
}

bool DeriveLeafAKeys(const PQSeed& seed, const Path& path, LeafAKeys& out)
{
    Secret32 node;
    Node(seed, path, node);
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    // Schnorr key: the first 32 bytes, retried with a counter in the (negligible) case that they
    // are not a valid secp256k1 scalar.
    for (uint8_t counter = 0;; ++counter) {
        Expand(LABEL_SCHNORR, node.b, buf, counter);
        out.schnorr.Set(buf, buf + 32, /*fCompressedIn=*/true);
        if (out.schnorr.IsValid()) break;
        if (counter == 255) {
            memory_cleanse(buf, sizeof(buf));
            return false;
        }
    }
    Expand(LABEL_MLDSA, node.b, buf);
    const bool ok{pqc::MLDSA44KeyFromSeed(std::span<const unsigned char>{buf, pqc::KEY_SEED_SIZE}, out.mldsa)};
    Expand(LABEL_SALT, node.b, buf);
    std::copy(buf, buf + SALT_SIZE, out.salt.begin());
    memory_cleanse(buf, sizeof(buf));
    return ok;
}

bool DeriveRecoveryKey(const PQSeed& seed, const Path& recovery_path, pqc::KeyPair& out)
{
    Secret32 node;
    Node(seed, recovery_path, node);
    unsigned char buf[CHMAC_SHA512::OUTPUT_SIZE];
    Expand(LABEL_SLHDSA, node.b, buf);
    const bool ok{pqc::SLHDSA128sKeyFromSeed(std::span<const unsigned char>{buf, pqc::KEY_SEED_SIZE}, out)};
    memory_cleanse(buf, sizeof(buf));
    return ok;
}

bool DeriveRecoveryPubKey(const PQSeed& seed, const Path& recovery_path, std::vector<unsigned char>& out)
{
    uint256 memo_key;
    {
        Secret32 node;
        Node(seed, recovery_path, node);
        CSHA256().Write(node.b.data(), node.b.size()).Finalize(memo_key.begin());
    }
    {
        LOCK(g_recovery_mutex);
        if (auto it{g_recovery_pubkeys.find(memo_key)}; it != g_recovery_pubkeys.end()) {
            out = it->second;
            return true;
        }
    }
    pqc::KeyPair key;
    if (!DeriveRecoveryKey(seed, recovery_path, key)) return false;
    out = key.pubkey;
    LOCK(g_recovery_mutex);
    if (g_recovery_pubkeys.size() >= MAX_RECOVERY_MEMO) g_recovery_pubkeys.clear();
    g_recovery_pubkeys.emplace(memo_key, out);
    return true;
}

bool DeriveAddressKeys(const PQSeed& seed, const Path& path, const Path& recovery_path, AddressKeys& out)
{
    LeafAKeys a;
    if (!DeriveLeafAKeys(seed, path, a)) return false;
    if (!DeriveRecoveryPubKey(seed, recovery_path, out.slhdsa)) return false;
    out.schnorr = XOnlyPubKey{a.schnorr.GetPubKey()};
    out.mldsa = a.mldsa.pubkey;
    out.salt = a.salt;
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

namespace {
int64_t Item(size_t size) { return GetSizeOfCompactSize(size) + size; }

SpendData SizeTemplate()
{
    SpendData t;
    t.keys.mldsa.assign(pqc::MLDSA44_PUBKEY_SIZE, 0);
    t.keys.slhdsa.assign(pqc::SLHDSA128S_PUBKEY_SIZE, 0);
    return t;
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
