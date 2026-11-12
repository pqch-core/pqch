// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: agent vaults (contrib/pqch/COFRE.md).
//
// A vault is a P2MR address whose hot leaf belongs to an agent (a program that pays by itself) and
// whose recovery leaf belongs to its owner, optionally after a relative timelock:
//   leaf A: <x-only> OP_CHECKSIGVERIFY <ML-DSA-44> OP_CHECKSIGMLDSA              (vault seed)
//   leaf B: [<N> OP_CHECKSEQUENCEVERIFY OP_DROP] <salt> OP_DROP <SLH-DSA> OP_CHECKSIGSLHDSA  (owner)
// The owner's wallet gives its recovery key (getvaultkey); the agent's wallet makes the vault from
// it (createvault) and hands the owner the import data; the owner recovers (recovervault).

#include <consensus/amount.h>
#include <core_io.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <util/rbf.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/p2mr.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/rpc/util.h>
#include <wallet/rpc/wallet.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>

#include <chainparams.h>
#include <policy/policy.h>

namespace wallet {
namespace {

/** Fee rate of a recovery when the node cannot estimate one and has no -fallbackfee: an emergency
 *  recovery must not fail for want of fee data (a quiet chain, a node just started). In quanta/vB. */
constexpr CAmount DEFAULT_RECOVERY_FEE_RATE{10};

std::string PathString(const p2mr::Path& path)
{
    std::string out;
    for (uint32_t step : path) out += strprintf("/%uh", step & ~p2mr::HARDENED);
    return out;
}

/** The wallet's own seed, or an RPC error. */
p2mr::PQSeed WalletSeedOrThrow(const CWallet& wallet)
{
    EnsureWalletIsUnlocked(wallet);
    auto seed{wallet.GetPQWalletSeed()};
    if (!seed) throw JSONRPCError(RPC_WALLET_ERROR, "This wallet has no post-quantum seed of its own (it was made without recovery words, or has private keys disabled)");
    return *seed;
}

struct VaultCoin {
    COutput coin;
    uint32_t older;
};

/** The coins of this wallet's vault descriptors, the owner's copies only if `owner`. */
std::vector<VaultCoin> ListVaultCoins(const CWallet& wallet, const DescriptorScriptPubKeyMan* only = nullptr) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    CCoinControl cc;
    cc.m_p2mr_recovery = true;         // so that the owner's copies are listed too
    cc.m_include_unsafe_inputs = true; // and coins not yet confirmed that someone else sent
    CoinFilterParams params;
    params.skip_locked = false;
    std::vector<VaultCoin> out;
    for (const COutput& coin : AvailableCoins(wallet, &cc, /*feerate=*/std::nullopt, params).All()) {
        for (const ScriptPubKeyMan* spkm : wallet.GetScriptPubKeyMans(coin.txout.scriptPubKey)) {
            const auto* desc{dynamic_cast<const DescriptorScriptPubKeyMan*>(spkm)};
            if (!desc || (only && desc != only)) continue;
            const auto vault{WITH_LOCK(desc->cs_desc_man, return desc->GetWalletDescriptor().descriptor->GetP2MRVault())};
            if (!vault) continue;
            out.push_back({coin, vault->second});
            break;
        }
    }
    return out;
}

} // namespace

RPCMethod getvaultkey()
{
    return RPCMethod{
        "getvaultkey",
        "PQCH: returns this wallet's recovery key number `index` for an agent's vault, as the slh(...) argument of createvault.\n"
        "Call it in the OWNER's wallet and give the key to the agent's wallet (it is public: only this wallet can sign with it).\n"
        "The key is an SLH-DSA-128s key made from this wallet's seed, so its recovery words restore it.\n"
        "Use a different index for each vault. Requires the wallet to be unlocked if it is encrypted.\n",
        {
            {"index", RPCArg::Type::NUM, RPCArg::Default{0}, "The key's number (0 to 2147483646)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "key", "slh([SEEDID/PATH]PUBKEY), for createvault"},
                {RPCResult::Type::NUM, "index", "The key's number"},
            }
        },
        RPCExamples{
            HelpExampleCli("getvaultkey", "")
            + HelpExampleCli("getvaultkey", "1")
            + HelpExampleRpc("getvaultkey", "0")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;
    const int64_t index{request.params[0].isNull() ? 0 : request.params[0].getInt<int64_t>()};
    if (index < 0 || index >= p2mr::RECOVERY_KEY_INDEX) throw JSONRPCError(RPC_INVALID_PARAMETER, "index must be from 0 to 2147483646");
    LOCK(pwallet->cs_wallet);
    const p2mr::PQSeed seed{WalletSeedOrThrow(*pwallet)};
    const p2mr::Path path{p2mr::VaultKeyPath(Params().IsTestChain(), static_cast<uint32_t>(index))};
    std::vector<unsigned char> pub;
    if (!p2mr::DeriveRecoveryPubKey(seed, path, pub)) throw JSONRPCError(RPC_WALLET_ERROR, "Could not derive the recovery key");
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("key", strprintf("slh([%s%s]%s)", HexStr(seed.Id()), PathString(path), HexStr(pub)));
    obj.pushKV("index", index);
    return obj;
},
    };
}

RPCMethod createvault()
{
    return RPCMethod{
        "createvault",
        "PQCH: makes vault number `index` of this wallet (the AGENT's) with the owner's recovery key, and returns what the owner imports.\n"
        "Every address of the vault can be spent at once by this wallet (hybrid leaf, as usual), or by the owner through the recovery\n"
        "leaf once the coin has `older` confirmations (0: at any time). With active=true (the default) the vault takes over this\n"
        "wallet's receiving and change addresses, so that change stays in the vault.\n"
        "The vault's seed comes from this wallet's seed, so its recovery words restore the vault (call createvault again with the same\n"
        "arguments, then rescanblockchain).\n"
        "WARNING: owner_import holds the vault's seed. Give it to the owner over a private channel: whoever has it can spend through\n"
        "the hot leaf. The owner imports it with importdescriptors.\n"
        "A timelock does not stop a thief who has this wallet's keys: it only decides when the owner can take the coins back.\n",
        {
            {"owner_key", RPCArg::Type::STR, RPCArg::Optional::NO, "The owner's recovery key, from getvaultkey in the owner's wallet: slh([SEEDID/PATH]PUBKEY)"},
            {"older", RPCArg::Type::NUM, RPCArg::Optional::NO, strprintf("Confirmations a coin needs before the owner can recover it (0 to %u; 1440 blocks are about a day)", p2mr::MAX_VAULT_OLDER)},
            {"index", RPCArg::Type::NUM, RPCArg::Default{0}, "The vault's number in this wallet"},
            {"active", RPCArg::Type::BOOL, RPCArg::Default{true}, "Whether the vault gives this wallet's new receiving and change addresses"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "descriptors", "The vault's descriptors (public form: receiving, change)", {{RPCResult::Type::STR, "", ""}}},
                {RPCResult::Type::ARR, "owner_import", "The request the owner passes to importdescriptors (holds the vault's seed)", {{RPCResult::Type::OBJ_DYN, "", "", {{RPCResult::Type::ANY, "", ""}}}}},
            }
        },
        RPCExamples{
            HelpExampleCli("createvault", "\"slh([...]...)\" 1440")
            + HelpExampleRpc("createvault", "\"slh([...]...)\", 1440")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;
    if (!IsPQWalletChain()) throw JSONRPCError(RPC_WALLET_ERROR, "Vaults exist only on PQCH chains");
    const std::string owner_key{request.params[0].get_str()};
    const int64_t older{request.params[1].getInt<int64_t>()};
    if (older < 0 || older > p2mr::MAX_VAULT_OLDER) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("older must be from 0 to %u", p2mr::MAX_VAULT_OLDER));
    const int64_t index{request.params[2].isNull() ? 0 : request.params[2].getInt<int64_t>()};
    if (index < 0 || index >= p2mr::RECOVERY_KEY_INDEX) throw JSONRPCError(RPC_INVALID_PARAMETER, "index must be from 0 to 2147483646");
    const bool active{request.params[3].isNull() || request.params[3].get_bool()};
    if (!owner_key.starts_with("slh(") || owner_key.find_first_of(",()", 4) != owner_key.size() - 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "owner_key must be slh([SEEDID/PATH]PUBKEY), from getvaultkey");
    }

    LOCK(pwallet->cs_wallet);
    const p2mr::PQSeed wallet_seed{WalletSeedOrThrow(*pwallet)};
    const p2mr::PQSeed vault_seed{p2mr::VaultSeed(wallet_seed, static_cast<uint32_t>(index))};
    const int64_t now{GetTime()};
    UniValue descriptors(UniValue::VARR), owner_import(UniValue::VARR);
    for (const bool internal : {false, true}) {
        const std::string desc_str{strprintf("p2mr_vault(pqprv(%s)/360h/%uh/%uh/%uh/*h,%s,older(%d))", HexStr(vault_seed.bytes),
                                             Params().IsTestChain() ? 1 : 0, p2mr::VAULT_ACCOUNT, internal ? 1 : 0, owner_key, older)};
        FlatSigningProvider keys;
        std::string error;
        auto parsed{Parse(desc_str, keys, error, /*require_checksum=*/false)};
        if (parsed.size() != 1) throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid owner_key or vault: " + error); // the parser's errors never quote the seed
        // The owner's key must come from another wallet: with its own key this wallet would own the
        // vault, so it could not spend from it as the agent (and its new addresses would be a vault
        // only it recovers).
        if (const auto vault{parsed[0]->GetP2MRVault()}; vault && vault->first == wallet_seed.Id()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "owner_key is this wallet's own recovery key. Make the vault in the agent's wallet, with the key from getvaultkey in the owner's wallet");
        }
        std::string priv;
        if (!parsed[0]->ToPrivateString(keys, priv)) throw JSONRPCError(RPC_WALLET_ERROR, "Could not write the vault descriptor");
        const std::string pub{parsed[0]->ToString()};

        WalletDescriptor w_desc{std::move(parsed[0]), static_cast<uint64_t>(now), 0, static_cast<int32_t>(pwallet->m_keypool_size), 0};
        auto spkm{pwallet->AddWalletDescriptor(w_desc, keys, "", internal)};
        if (!spkm) throw JSONRPCError(RPC_WALLET_ERROR, "Could not add the vault: " + util::ErrorString(spkm).original);
        if (active) pwallet->AddActiveScriptPubKeyMan(spkm->get().GetID(), OutputType::BECH32M, internal);

        descriptors.push_back(pub);
        UniValue item(UniValue::VOBJ);
        item.pushKV("desc", priv);
        item.pushKV("timestamp", now);
        item.pushKV("active", false);
        item.pushKV("internal", internal);
        UniValue range(UniValue::VARR);
        range.push_back(0);
        // The owner's wallet tops the range up as it sees the addresses used, but only those it has:
        // a range at least as wide as a default key pool covers the agent's unused addresses.
        range.push_back(std::max<int64_t>(pwallet->m_keypool_size, DEFAULT_KEYPOOL_SIZE) - 1);
        item.pushKV("range", range);
        owner_import.push_back(item);
    }
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("descriptors", descriptors);
    obj.pushKV("owner_import", owner_import);
    return obj;
},
    };
}

RPCMethod listvaults()
{
    return RPCMethod{
        "listvaults",
        "PQCH: lists this wallet's vault descriptors (contrib/pqch/COFRE.md), as agent (it spends through the hot leaf) or owner\n"
        "(it recovers through the recovery leaf), with their coins.\n",
        {},
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR, "desc", "The vault's descriptor (public form)"},
                    {RPCResult::Type::STR, "role", "agent or owner"},
                    {RPCResult::Type::BOOL, "internal", "Whether these are change addresses"},
                    {RPCResult::Type::BOOL, "active", "Whether it gives this wallet's new addresses"},
                    {RPCResult::Type::NUM, "older", "Confirmations a coin needs before the owner can recover it"},
                    {RPCResult::Type::STR_AMOUNT, "amount", "Value of its coins, confirmed or not"},
                    {RPCResult::Type::STR_AMOUNT, "unconfirmed", "Part of the amount not yet confirmed"},
                    {RPCResult::Type::STR_AMOUNT, "recoverable", "Value the owner can recover now"},
                    {RPCResult::Type::NUM, "next_recoverable_in", /*optional=*/true, "Blocks until the next coin can be recovered, if any is waiting"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("listvaults", "")
            + HelpExampleRpc("listvaults", "")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;
    LOCK(pwallet->cs_wallet);
    const auto active{pwallet->GetActiveScriptPubKeyMans()};
    UniValue out(UniValue::VARR);
    for (ScriptPubKeyMan* spkm : pwallet->GetAllScriptPubKeyMans()) {
        const auto* desc{dynamic_cast<const DescriptorScriptPubKeyMan*>(spkm)};
        if (!desc) continue;
        const auto [vault, desc_str]{WITH_LOCK(desc->cs_desc_man, return std::pair(desc->GetWalletDescriptor().descriptor->GetP2MRVault(), desc->GetWalletDescriptor().descriptor->ToString()))};
        if (!vault) continue;
        CAmount amount{0}, unconfirmed{0}, recoverable{0};
        std::optional<int> next;
        for (const VaultCoin& vc : ListVaultCoins(*pwallet, desc)) {
            amount += vc.coin.txout.nValue;
            if (vc.coin.depth == 0) unconfirmed += vc.coin.txout.nValue;
            if (vc.coin.depth > 0 && vc.coin.depth >= static_cast<int>(vc.older)) {
                recoverable += vc.coin.txout.nValue;
            } else {
                const int left{std::max(1, static_cast<int>(vc.older) - vc.coin.depth)};
                if (!next || left < *next) next = left;
            }
        }
        UniValue item(UniValue::VOBJ);
        item.pushKV("desc", desc_str);
        item.pushKV("role", desc->IsP2MRVaultOwner() ? "owner" : "agent");
        // Only active descriptors are marked internal in the wallet: otherwise go by the path that
        // createvault gives the change addresses, .../2h/1h/*h.
        item.pushKV("internal", pwallet->IsInternalScriptPubKeyMan(spkm).value_or(desc_str.find(strprintf("/%uh/1h/*h,", p2mr::VAULT_ACCOUNT)) != std::string::npos));
        item.pushKV("active", active.contains(spkm));
        item.pushKV("older", vault->second);
        item.pushKV("amount", ValueFromAmount(amount));
        item.pushKV("unconfirmed", ValueFromAmount(unconfirmed));
        item.pushKV("recoverable", ValueFromAmount(recoverable));
        if (next) item.pushKV("next_recoverable_in", *next);
        out.push_back(item);
    }
    return out;
},
    };
}

RPCMethod recovervault()
{
    return RPCMethod{
        "recovervault",
        "PQCH: the OWNER takes back the coins of the agents' vaults this wallet owns (listvaults, role owner): every coin with enough\n"
        "confirmations is sent to `address` in one transaction, spent through the recovery leaf (SLH-DSA-128s).\n"
        "Coins that are still too young, or not yet confirmed, are left and listed under `waiting`. Requires the wallet to be unlocked if it is encrypted.\n"
        "If the agent's keys were stolen, recover at once and stop using the vault: the thief can spend through the hot leaf too.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::DefaultHint{"a new address of this wallet, never a vault's"}, "Where the coins go (not an address of a vault this wallet owns)"},
            {"fee_rate", RPCArg::Type::AMOUNT, RPCArg::DefaultHint{"estimate"}, "Fee rate in " + CURRENCY_ATOM + "/vB. Without it the node's estimate is used, or, "
             "if the node has none and no -fallbackfee, " + util::ToString(DEFAULT_RECOVERY_FEE_RATE) + " " + CURRENCY_ATOM + "/vB (more if the mempool requires it)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The recovery transaction"},
                {RPCResult::Type::NUM, "inputs", "Number of coins recovered"},
                {RPCResult::Type::STR_AMOUNT, "amount", "Value sent to the address (after the fee)"},
                {RPCResult::Type::STR_AMOUNT, "fee", "The fee"},
                {RPCResult::Type::NUM, "fee_rate", "The fee rate paid, in " + CURRENCY_ATOM + "/vB"},
                {RPCResult::Type::STR, "fee_source", "given (fee_rate), estimated (the node's estimate or -fallbackfee) or default (no estimate: the default rate above)"},
                {RPCResult::Type::ARR, "waiting", "Coins not yet recoverable", {
                    {RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR_HEX, "txid", ""},
                        {RPCResult::Type::NUM, "vout", ""},
                        {RPCResult::Type::STR_AMOUNT, "amount", ""},
                        {RPCResult::Type::NUM, "blocks_left", "Blocks until it can be recovered"},
                    }},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("recovervault", "")
            + HelpExampleCli("recovervault", "\"" + EXAMPLE_ADDRESS[0] + "\" 2")
            + HelpExampleRpc("recovervault", "\"" + EXAMPLE_ADDRESS[0] + "\"")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;
    pwallet->BlockUntilSyncedToCurrentChain();
    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(*pwallet);

    CTxDestination dest;
    if (request.params[0].isNull()) {
        // Never a vault address, even when a vault gives this wallet's new addresses: the coins
        // would go back into a vault.
        auto res{pwallet->GetNewPQMainDestination("")};
        if (!res) throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(res).original);
        dest = *res;
    } else {
        dest = DecodeDestination(request.params[0].get_str());
        if (!IsValidDestination(dest)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        if (pwallet->IsP2MRVaultOwnerScript(GetScriptForDestination(dest))) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "This address is in a vault this wallet owns: the coins would go back into the vault");
        }
    }

    CCoinControl cc;
    cc.m_p2mr_recovery = true;
    cc.m_allow_other_inputs = false;
    std::string fee_source{"given"};
    if (!request.params[1].isNull()) {
        cc.m_feerate = CFeeRate{AmountFromValue(request.params[1], /*decimals=*/3)};
    } else if (GetMinimumFeeRate(*pwallet, cc).fee_reason == FeeReason::FALLBACK && !pwallet->m_allow_fallback_fee) {
        // No estimate and no -fallbackfee: CreateTransaction would refuse. Use the default rate, but
        // never below what the mempool and the wallet's minimums require.
        cc.m_feerate = std::max({CFeeRate{DEFAULT_RECOVERY_FEE_RATE * 1000}, pwallet->chain().mempoolMinFee(), GetRequiredFeeRate(*pwallet)});
        fee_source = "default";
    } else {
        fee_source = "estimated";
    }

    CAmount total{0};
    int inputs{0};
    UniValue waiting(UniValue::VARR);
    for (const VaultCoin& vc : ListVaultCoins(*pwallet)) {
        if (!pwallet->IsP2MRVaultOwnerScript(vc.coin.txout.scriptPubKey)) continue;
        // Never a coin without a confirmation: with a timelock the consensus would refuse it, and with
        // older(0) whoever sent it can still replace it.
        if (vc.coin.depth == 0 || vc.coin.depth < static_cast<int>(vc.older) || pwallet->IsLockedCoin(vc.coin.outpoint)) {
            UniValue w(UniValue::VOBJ);
            w.pushKV("txid", vc.coin.outpoint.hash.GetHex());
            w.pushKV("vout", vc.coin.outpoint.n);
            w.pushKV("amount", ValueFromAmount(vc.coin.txout.nValue));
            w.pushKV("blocks_left", std::max(vc.coin.depth == 0 ? 1 : 0, static_cast<int>(vc.older) - vc.coin.depth));
            waiting.push_back(w);
            continue;
        }
        // BIP68: a relative timelock in blocks is the input's nSequence (the timelock's CSV checks it).
        cc.Select(vc.coin.outpoint).SetSequence(vc.older > 0 ? vc.older : MAX_BIP125_RBF_SEQUENCE);
        total += vc.coin.txout.nValue;
        ++inputs;
    }
    if (inputs == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, waiting.empty() ? "This wallet owns no vault coins to recover" :
                           strprintf("No vault coin can be recovered yet (%u waiting: see listvaults)", waiting.size()));
    }

    auto res{CreateTransaction(*pwallet, {CRecipient{dest, total, /*fSubtractFeeFromAmount=*/true}}, /*change_pos=*/std::nullopt, cc, /*sign=*/true)};
    if (!res) throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(res).original);
    pwallet->CommitTransaction(res->tx, {}, {});

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", res->tx->GetHash().GetHex());
    obj.pushKV("inputs", inputs);
    obj.pushKV("amount", ValueFromAmount(total - res->fee));
    obj.pushKV("fee", ValueFromAmount(res->fee));
    const int64_t vsize{GetVirtualTransactionSize(*res->tx)};
    obj.pushKV("fee_rate", UniValue{UniValue::VNUM, strprintf("%.3f", static_cast<double>(res->fee) / vsize)});
    obj.pushKV("fee_source", fee_source);
    obj.pushKV("waiting", waiting);
    return obj;
},
    };
}

} // namespace wallet
