// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pqchwallet/pqchwallet.h>

#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <key.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/bip39.h>
#include <script/descriptor.h>
#include <script/p2mr.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <serialize.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/rbf.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{nullptr};

namespace {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::mutex g_mutex;
std::unique_ptr<ECC_Context> g_ecc;
std::optional<ChainType> g_chain;

//! Smallest fee rate the nodes relay (DEFAULT_MIN_RELAY_TX_FEE, in quanta per kvB → per vB).
constexpr double MIN_FEE_RATE{DEFAULT_MIN_RELAY_TX_FEE / 1000.0};
//! A fee rate above this is surely a mistake (a unit confusion).
constexpr double MAX_FEE_RATE{10'000};

const UniValue& Field(const UniValue& p, const std::string& key)
{
    const UniValue& v{p.find_value(key)};
    if (v.isNull()) throw Error(strprintf("missing parameter \"%s\"", key));
    return v;
}

std::string GetStr(const UniValue& p, const std::string& key)
{
    const UniValue& v{Field(p, key)};
    if (!v.isStr()) throw Error(strprintf("\"%s\" must be a string", key));
    return v.get_str();
}

std::string GetStrOr(const UniValue& p, const std::string& key, const std::string& fallback)
{
    return p.find_value(key).isNull() ? fallback : GetStr(p, key);
}

int64_t GetInt(const UniValue& p, const std::string& key, int64_t min, int64_t max)
{
    const UniValue& v{Field(p, key)};
    if (!v.isNum()) throw Error(strprintf("\"%s\" must be a number", key));
    const int64_t n{v.getInt<int64_t>()};
    if (n < min || n > max) throw Error(strprintf("\"%s\" must be between %d and %d", key, min, max));
    return n;
}

bool GetBoolOr(const UniValue& p, const std::string& key, bool fallback)
{
    const UniValue& v{p.find_value(key)};
    if (v.isNull()) return fallback;
    if (!v.isBool()) throw Error(strprintf("\"%s\" must be true or false", key));
    return v.get_bool();
}

void RequireInit()
{
    if (!g_chain) throw Error("call init first");
}

//! The words as typed on a phone (capitals, extra spaces): the BIP39 list is lowercase.
std::string Words(const UniValue& p)
{
    std::string words{GetStr(p, "mnemonic")};
    for (char& c : words) c = ToLower(c);
    return words;
}

p2mr::PQSeed SeedOf(const UniValue& p)
{
    std::string words{Words(p)};
    std::string passphrase{GetStrOr(p, "passphrase", "")};
    const auto seed{p2mr::SeedFromMnemonic(words, passphrase)};
    memory_cleanse(words.data(), words.size());
    memory_cleanse(passphrase.data(), passphrase.size());
    if (!seed) throw Error("the recovery words are not valid (24 words from the English BIP39 list)");
    return *seed;
}

/** The wallet's descriptor for one branch (0 receive, 1 change), as PQCH Core makes it (wallet/walletutil.cpp). */
std::unique_ptr<Descriptor> BranchDescriptor(const p2mr::PQSeed& seed, uint32_t branch, FlatSigningProvider& keys)
{
    std::string hex{HexStr(seed.bytes)};
    std::string desc{strprintf("p2mr(pqprv(%s)/360h/%sh/0h/%sh/*h)", hex, Params().IsTestChain() ? "1" : "0", branch)};
    std::string error;
    auto parsed{Parse(desc, keys, error, /*require_checksum=*/false)};
    memory_cleanse(hex.data(), hex.size());
    memory_cleanse(desc.data(), desc.size());
    if (parsed.size() != 1) throw Error("cannot build the wallet descriptor: " + error);
    return std::move(parsed[0]);
}

/** The output script of address `index` of a branch; its signing data goes into `provider`. */
CScript Derive(const Descriptor& desc, uint32_t index, FlatSigningProvider& provider)
{
    std::vector<CScript> scripts;
    FlatSigningProvider out;
    if (!desc.Expand(index, provider, scripts, out) || scripts.size() != 1) throw Error(strprintf("cannot derive address %d", index));
    provider.Merge(std::move(out));
    return scripts[0];
}

std::string AddressOf(const CScript& script)
{
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) throw Error("cannot encode the address");
    return EncodeDestination(dest);
}

CScript ScriptOfAddress(const std::string& address)
{
    std::string error;
    const CTxDestination dest{DecodeDestination(address, error)};
    if (!IsValidDestination(dest)) throw Error(error.empty() ? "invalid address" : "invalid address: " + error);
    const CScript script{GetScriptForDestination(dest)};
    // Only P2MR outputs exist on PQCH (witness v1); anything else would be refused by the nodes.
    int version;
    std::vector<unsigned char> program;
    if (!script.IsWitnessProgram(version, program) || version != 1 || program.size() != 32) {
        throw Error("not a PQCH (P2MR) address");
    }
    return script;
}

CAmount Dust(const CScript& script)
{
    return GetDustThreshold(CTxOut(0, script), CFeeRate(DUST_RELAY_TX_FEE));
}

/** Virtual size of a transaction spending `inputs` standard P2MR outputs (hybrid leaf) to `outputs`. */
/** `recovery`: inputs signed through the SLH-DSA recovery leaf (after the ML-DSA emergency soft fork). */
int64_t EstimateVSize(size_t inputs, const std::vector<CTxOut>& outputs, bool recovery)
{
    CMutableTransaction tx;
    tx.vin.resize(inputs);
    tx.vout = outputs;
    const int64_t base{static_cast<int64_t>(GetSerializeSize(TX_NO_WITNESS(tx)))};
    const int64_t weight{base * WITNESS_SCALE_FACTOR + 2 /* marker and flag */ + static_cast<int64_t>(inputs) * (recovery ? p2mr::RecoveryWitnessSize(false) : p2mr::HybridWitnessSize(false))};
    return (weight + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
}

CAmount FeeFor(double rate, int64_t vsize)
{
    return static_cast<CAmount>(std::ceil(rate * vsize));
}

UniValue Init(const UniValue& p)
{
    const std::string chain{GetStr(p, "chain")};
    ChainType type;
    if (chain == "main") type = ChainType::MAIN;
    else if (chain == "test") type = ChainType::TESTNET;
    else if (chain == "regtest") type = ChainType::REGTEST;
    else throw Error("chain must be \"main\", \"test\" or \"regtest\"");
    if (!g_ecc) g_ecc = std::make_unique<ECC_Context>();
    SelectParams(type);
    g_chain = type;
    UniValue r(UniValue::VOBJ);
    r.pushKV("chain", chain);
    r.pushKV("coinbase_maturity", Params().GetConsensus().nCoinbaseMaturity);
    r.pushKV("block_seconds", Params().GetConsensus().nPowTargetSpacing);
    return r;
}

UniValue NewMnemonic(const UniValue&)
{
    std::array<unsigned char, bip39::ENTROPY_SIZE> entropy;
    GetStrongRandBytes(entropy);
    std::string words{bip39::MnemonicFromEntropy(entropy)};
    memory_cleanse(entropy.data(), entropy.size());
    UniValue r(UniValue::VOBJ);
    r.pushKV("mnemonic", words);
    memory_cleanse(words.data(), words.size());
    return r;
}

UniValue CheckMnemonic(const UniValue& p)
{
    std::string words{Words(p)};
    const auto normalized{bip39::NormalizeMnemonic(words)};
    const bool valid{normalized && bip39::EntropyFromMnemonic(*normalized)};
    memory_cleanse(words.data(), words.size());
    UniValue r(UniValue::VOBJ);
    r.pushKV("valid", valid);
    if (valid) r.pushKV("normalized", *normalized);
    return r;
}

UniValue WalletId(const UniValue& p)
{
    UniValue r(UniValue::VOBJ);
    r.pushKV("id", SeedOf(p).Id().GetHex());
    return r;
}

UniValue Addresses(const UniValue& p)
{
    RequireInit();
    const p2mr::PQSeed seed{SeedOf(p)};
    const auto branch{static_cast<uint32_t>(GetInt(p, "branch", 0, 1))};
    const auto from{static_cast<uint32_t>(GetInt(p, "from", 0, p2mr::HARDENED - 1))};
    const auto count{static_cast<uint32_t>(GetInt(p, "count", 1, 1000))};
    if (uint64_t{from} + count > p2mr::HARDENED) throw Error("index out of range");
    FlatSigningProvider keys;
    const auto desc{BranchDescriptor(seed, branch, keys)};
    UniValue list(UniValue::VARR);
    for (uint32_t i{from}; i < from + count; ++i) {
        const CScript script{Derive(*desc, i, keys)};
        UniValue a(UniValue::VOBJ);
        a.pushKV("index", static_cast<int64_t>(i));
        a.pushKV("address", AddressOf(script));
        a.pushKV("script", HexStr(script));
        list.push_back(std::move(a));
    }
    return list;
}

UniValue CheckAddress(const UniValue& p)
{
    RequireInit();
    UniValue r(UniValue::VOBJ);
    try {
        const CScript script{ScriptOfAddress(GetStr(p, "address"))};
        r.pushKV("valid", true);
        r.pushKV("script", HexStr(script));
    } catch (const Error& e) {
        r.pushKV("valid", false);
        r.pushKV("error", e.what());
    }
    return r;
}

struct Utxo {
    COutPoint outpoint;
    CAmount value;
    uint32_t branch;
    uint32_t index;
    CScript script;
};

UniValue CreateTx(const UniValue& p)
{
    RequireInit();
    const p2mr::PQSeed seed{SeedOf(p)};
    FlatSigningProvider provider;
    const std::unique_ptr<Descriptor> branches[2]{BranchDescriptor(seed, 0, provider), BranchDescriptor(seed, 1, provider)};

    // Coins
    const UniValue list{Field(p, "utxos")};
    if (!list.isArray() || list.empty()) throw Error("\"utxos\" must be a non-empty list");
    std::vector<Utxo> utxos;
    for (const UniValue& u : list.getValues()) {
        if (!u.isObject()) throw Error("each utxo is an object");
        const auto txid{Txid::FromHex(GetStr(u, "txid"))};
        if (!txid) throw Error("utxo txid is not 64 hex digits");
        Utxo c;
        c.outpoint = COutPoint(*txid, static_cast<uint32_t>(GetInt(u, "vout", 0, 1'000'000)));
        c.value = GetInt(u, "value", 1, MAX_MONEY);
        c.branch = static_cast<uint32_t>(GetInt(u, "branch", 0, 1));
        c.index = static_cast<uint32_t>(GetInt(u, "index", 0, p2mr::HARDENED - 1));
        c.script = Derive(*branches[c.branch], c.index, provider);
        for (const Utxo& other : utxos) {
            if (other.outpoint == c.outpoint) throw Error("the same utxo is listed twice");
        }
        utxos.push_back(std::move(c));
    }
    // Largest first: fewest inputs, so the smallest transaction (each P2MR input is ~1 kvB).
    std::stable_sort(utxos.begin(), utxos.end(), [](const Utxo& a, const Utxo& b) { return a.value > b.value; });

    // Payment
    const CScript to{ScriptOfAddress(GetStr(p, "to"))};
    const bool send_all{GetBoolOr(p, "send_all", false)};
    const bool subtract_fee{send_all || GetBoolOr(p, "subtract_fee", false)};
    const CAmount amount{send_all ? 0 : GetInt(p, "amount", 1, MAX_MONEY)};
    const UniValue rate_v{Field(p, "fee_rate")};
    if (!rate_v.isNum()) throw Error("\"fee_rate\" must be a number (quanta per vB)");
    const double fee_rate{std::max(MIN_FEE_RATE, rate_v.get_real())};
    if (!(fee_rate <= MAX_FEE_RATE)) throw Error(strprintf("fee rate above %d quanta/vB: surely a mistake", MAX_FEE_RATE));
    const auto change_index{static_cast<uint32_t>(GetInt(p, "change_index", 0, p2mr::HARDENED - 1))};
    const CScript change_script{Derive(*branches[1], change_index, provider)};
    const CAmount dust_to{Dust(to)};
    const CAmount dust_change{Dust(change_script)};
    // PQCH: after the ML-DSA emergency soft fork (SEGURANCA.md) the coins can only be spent through the
    // SLH-DSA recovery leaf: larger inputs (~7.9 kB of witness), so a higher fee for the same rate.
    const bool recovery{GetBoolOr(p, "recovery", false)};

    // Selection: the first k coins (largest first) that pay amount and fee.
    size_t used{0};
    CAmount pay{0}, change{0}, fee{0};
    CAmount total{0};
    for (size_t k{1}; k <= utxos.size() && used == 0; ++k) {
        total += utxos[k - 1].value;
        if (send_all && k < utxos.size()) continue; // send_all spends every coin
        const CAmount fee_with_change{FeeFor(fee_rate, EstimateVSize(k, {CTxOut(0, to), CTxOut(0, change_script)}, recovery))};
        const CAmount fee_without{FeeFor(fee_rate, EstimateVSize(k, {CTxOut(0, to)}, recovery))};
        if (subtract_fee) {
            const CAmount target{send_all ? total : amount};
            if (total < target) continue;
            const CAmount rest{total - target};
            if (!send_all && rest - 0 >= dust_change && target - fee_with_change >= dust_to) {
                pay = target - fee_with_change;
                change = rest;
                fee = fee_with_change;
            } else {
                pay = target - fee_without;
                change = 0;
                fee = total - pay; // what is left below dust goes to the fee
            }
            if (pay < dust_to) throw Error("the amount does not cover the fee");
            used = k;
        } else {
            if (total >= amount + fee_with_change && total - amount - fee_with_change >= dust_change) {
                pay = amount;
                change = total - amount - fee_with_change;
                fee = fee_with_change;
                used = k;
            } else if (total >= amount + fee_without) {
                pay = amount;
                change = 0;
                fee = total - amount; // a change below dust goes to the fee
                used = k;
            }
        }
    }
    if (used == 0) {
        CAmount available{0};
        for (const Utxo& u : utxos) available += u.value;
        throw Error(strprintf("insufficient funds: %s quanta available, not enough for the amount and its fee", available));
    }
    if (!send_all && !subtract_fee && pay < dust_to) throw Error("amount too small (dust)");

    // Transaction (version 2, RBF so the fee can be raised later).
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.nLockTime = static_cast<uint32_t>(p.find_value("locktime").isNull() ? 0 : GetInt(p, "locktime", 0, 499'999'999));
    std::map<COutPoint, Coin> coins;
    UniValue inputs(UniValue::VARR);
    for (size_t i{0}; i < used; ++i) {
        mtx.vin.emplace_back(utxos[i].outpoint, CScript(), MAX_BIP125_RBF_SEQUENCE);
        coins.emplace(utxos[i].outpoint, Coin(CTxOut(utxos[i].value, utxos[i].script), 1, false));
        inputs.push_back(utxos[i].outpoint.hash.GetHex() + ":" + util::ToString(utxos[i].outpoint.n));
    }
    mtx.vout.emplace_back(pay, to);
    if (change > 0) {
        mtx.vout.emplace_back(change, change_script);
        // The change is not always the last output.
        if (FastRandomContext().randbool()) std::swap(mtx.vout[0], mtx.vout[1]);
    }
    std::map<int, bilingual_str> errors;
    SignOptions sign_options;
    sign_options.p2mr_recovery = recovery;
    if (!SignTransaction(mtx, &provider, coins, sign_options, errors) || !errors.empty()) {
        throw Error("signing failed: " + (errors.empty() ? std::string{"unknown"} : errors.begin()->second.original));
    }
    const CTransaction tx{mtx};
    const int64_t vsize{GetVirtualTransactionSize(tx)};
    if (FeeFor(MIN_FEE_RATE, vsize) > fee) throw Error("internal error: fee below the minimum relay fee");

    UniValue r(UniValue::VOBJ);
    r.pushKV("hex", EncodeHexTx(tx));
    r.pushKV("txid", tx.GetHash().GetHex());
    r.pushKV("fee", fee);
    r.pushKV("vsize", vsize);
    r.pushKV("amount", pay);
    r.pushKV("recovery", recovery);
    r.pushKV("inputs", std::move(inputs));
    if (change > 0) {
        UniValue c(UniValue::VOBJ);
        c.pushKV("address", AddressOf(change_script));
        c.pushKV("value", change);
        c.pushKV("index", static_cast<int64_t>(change_index));
        r.pushKV("change", std::move(c));
    } else {
        r.pushKV("change", UniValue{});
    }
    return r;
}

char* Copy(const std::string& s)
{
    char* out{static_cast<char*>(std::malloc(s.size() + 1))};
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

} // namespace

extern "C" char* pqchw_call(const char* method, const char* params_json)
{
    UniValue reply(UniValue::VOBJ);
    try {
        std::lock_guard<std::mutex> lock{g_mutex};
        if (!method) throw Error("no method");
        UniValue params(UniValue::VOBJ);
        if (params_json && *params_json && (!params.read(params_json) || !params.isObject())) throw Error("params must be a JSON object");
        const std::string m{method};
        UniValue result;
        if (m == "init") result = Init(params);
        else if (m == "new_mnemonic") result = NewMnemonic(params);
        else if (m == "check_mnemonic") result = CheckMnemonic(params);
        else if (m == "wallet_id") result = WalletId(params);
        else if (m == "addresses") result = Addresses(params);
        else if (m == "check_address") result = CheckAddress(params);
        else if (m == "create_tx") result = CreateTx(params);
        else throw Error("unknown method " + m);
        reply.pushKV("ok", true);
        reply.pushKV("result", std::move(result));
    } catch (const std::exception& e) {
        reply = UniValue(UniValue::VOBJ);
        reply.pushKV("ok", false);
        reply.pushKV("error", e.what());
    }
    return Copy(reply.write());
}

extern "C" void pqchw_free(char* str)
{
    if (!str) return;
    memory_cleanse(str, std::strlen(str));
    std::free(str);
}
