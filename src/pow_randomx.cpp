// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow_randomx.h>

#include <auxpow.h>

#include <chain.h>
#include <logging.h>
#include <pow.h>
#include <primitives/block.h>
#include <span.h>
#include <streams.h>
#include <sync.h>
#include <util/hasher.h>
#include <util/time.h>

#include <randomx.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::atomic<bool> g_randomx_fast_mining{false};
std::atomic<unsigned> g_randomx_mining_threads{1};

namespace {

/** Flags for the light-mode cache (V2 only affects the virtual machine, not the cache). */
randomx_flags CacheFlags(bool allow_jit)
{
    int f = randomx_get_flags();
    f &= ~(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_LARGE_PAGES | RANDOMX_FLAG_V2);
    if (!allow_jit) f &= ~(RANDOMX_FLAG_JIT | RANDOMX_FLAG_SECURE);
    return randomx_flags(f);
}

/** Flags for a virtual machine running RandomX v2 (PQCH's own proof of work) or, with v2 false, the
 *  original RandomX that Monero uses before its hard fork 17 (merged mining). */
randomx_flags VmFlags(bool allow_jit, bool full_mem, bool v2 = true)
{
    int f = CacheFlags(allow_jit);
    if (v2) f |= RANDOMX_FLAG_V2;
    if (full_mem) f |= RANDOMX_FLAG_FULL_MEM;
    return randomx_flags(f);
}

/** A light-mode cache and two VMs for one key: RandomX v2 and the original RandomX (v1). The cache
 *  does not depend on the version, so both VMs share it. The VMs are not thread-safe, hence the mutex. */
struct RxContext {
    std::vector<unsigned char> key;
    randomx_cache* cache{nullptr};
    randomx_vm* vm{nullptr};    //!< RandomX v2
    randomx_vm* vm_v1{nullptr}; //!< original RandomX (Monero blocks before hard fork 17)
    std::mutex vm_mutex;
    bool jit{false};            //!< flags the cache was made with (extra VMs must match them)

    explicit RxContext(std::span<const unsigned char> k) : key(k.begin(), k.end())
    {
        for (bool jit : {true, false}) {
            cache = randomx_alloc_cache(CacheFlags(jit));
            if (!cache) continue;
            randomx_init_cache(cache, key.data(), key.size());
            vm = randomx_create_vm(VmFlags(jit, /*full_mem=*/false), cache, nullptr);
            vm_v1 = randomx_create_vm(VmFlags(jit, /*full_mem=*/false, /*v2=*/false), cache, nullptr);
            if (vm && vm_v1) {
                this->jit = jit;
                return;
            }
            if (vm) randomx_destroy_vm(vm);
            if (vm_v1) randomx_destroy_vm(vm_v1);
            vm = vm_v1 = nullptr;
            randomx_release_cache(cache);
            cache = nullptr;
        }
        throw std::runtime_error("RandomX: could not allocate cache/VM (256 MiB of memory are required)");
    }
    ~RxContext()
    {
        if (vm) randomx_destroy_vm(vm);
        if (vm_v1) randomx_destroy_vm(vm_v1);
        if (cache) randomx_release_cache(cache);
    }
};

// Keep the two most recently used keys: around an epoch change and during reorgs blocks
// from both sides of the boundary are validated, and rebuilding a cache costs ~0.5 s.
constexpr size_t MAX_RX_CONTEXTS{4}; // PQCH: direct keys and Monero keys (merged mining)
std::mutex g_rx_mutex;
std::deque<std::shared_ptr<RxContext>> g_rx_contexts; // most recently used first

std::shared_ptr<RxContext> GetContext(std::span<const unsigned char> key)
{
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    auto it = std::find_if(g_rx_contexts.begin(), g_rx_contexts.end(), [&](const auto& ctx) {
        return std::ranges::equal(ctx->key, key);
    });
    if (it != g_rx_contexts.end()) {
        auto ctx = *it;
        g_rx_contexts.erase(it);
        g_rx_contexts.push_front(ctx);
        return ctx;
    }
    const auto start{SteadyClock::now()};
    auto ctx = std::make_shared<RxContext>(key);
    LogDebug(BCLog::VALIDATION, "RandomX: initialised light cache in %d ms\n",
             Ticks<std::chrono::milliseconds>(SteadyClock::now() - start));
    g_rx_contexts.push_front(ctx);
    while (g_rx_contexts.size() > MAX_RX_CONTEXTS) g_rx_contexts.pop_back();
    return ctx;
}

} // namespace

bool RandomXKeyCached(std::span<const unsigned char> key)
{
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    return std::any_of(g_rx_contexts.begin(), g_rx_contexts.end(), [&](const auto& ctx) { return std::ranges::equal(ctx->key, key); });
}

namespace {
std::vector<unsigned char> SerializeHeader(const CBlockHeader& header)
{
    auto bytes{header.SerializePure()};
    assert(bytes.size() == 80);
    return bytes;
}

} // namespace

int GetRandomXSeedHeight(int height, const Consensus::Params& params)
{
    const int interval{params.nRandomXSeedInterval};
    const int lag{params.nRandomXSeedLag};
    assert(interval > 0 && (interval & (interval - 1)) == 0); // power of two
    assert(lag >= 0);
    if (height <= interval + lag) return 0;
    return (height - lag - 1) & ~(interval - 1);
}

uint256 GetRandomXSeedHash(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    assert(pindexPrev != nullptr);
    const int seed_height{GetRandomXSeedHeight(pindexPrev->nHeight + 1, params)};
    const CBlockIndex* seed_block{pindexPrev->GetAncestor(seed_height)};
    assert(seed_block != nullptr);
    return seed_block->GetBlockHash();
}

namespace {
uint256 RandomXHashVersion(std::span<const unsigned char> key, std::span<const unsigned char> input, bool v2)
{
    auto ctx{GetContext(key)};
    uint256 out;
    std::lock_guard<std::mutex> lock(ctx->vm_mutex);
    randomx_calculate_hash(v2 ? ctx->vm : ctx->vm_v1, input.data(), input.size(), out.data());
    return out;
}
} // namespace

uint256 RandomXHash(std::span<const unsigned char> key, std::span<const unsigned char> input)
{
    return RandomXHashVersion(key, input, /*v2=*/true);
}

uint256 RandomXHashV1(std::span<const unsigned char> key, std::span<const unsigned char> input)
{
    return RandomXHashVersion(key, input, /*v2=*/false);
}

uint256 RandomXCommitment(std::span<const unsigned char> input, const uint256& hash)
{
    uint256 out;
    randomx_calculate_commitment(input.data(), input.size(), hash.data(), out.data());
    return out;
}

uint256 MoneroPowHash(const uint256& seed, std::span<const unsigned char> blob)
{
    // The hashing blob starts with the Monero major version (a varint; every value from 17 on, in one
    // byte or more, starts with a byte >= 17). Same rule as P2Pool (HARDFORK_VERSION_RANDOMX_V2).
    const bool v2{!blob.empty() && blob[0] >= MONERO_HF_RANDOMX_V2};
    uint256 hash{RandomXHashVersion(std::span<const unsigned char>{seed.data(), seed.size()}, blob, v2)};
    // From hard fork 17 Monero's proof of work is the RandomX v2 commitment:
    // Blake2b-256(hashing blob || RandomX v2 hash), as in Monero and P2Pool v5 (randomx_calculate_commitment).
    return v2 ? RandomXCommitment(blob, hash) : hash;
}

namespace {
// PQCH: headers whose direct RandomX proof of work was checked ahead of validation, outside cs_main
// (PreverifyHeadersPow). Keyed by the header id (SHA256d of all 80 bytes, nBits and nonce included),
// with the key it was checked under: the fact "RandomX(seed, header) meets header.nBits" depends on
// nothing else, so validation can use it instead of hashing again. Bounded, oldest out first.
constexpr size_t MAX_PREVERIFIED{20000};
std::mutex g_preverified_mutex;
std::unordered_map<uint256, uint256, SaltedUint256Hasher> g_preverified;
std::deque<uint256> g_preverified_order;

bool TakePreverified(const uint256& id, const uint256& seed)
{
    std::lock_guard<std::mutex> lock(g_preverified_mutex);
    auto it{g_preverified.find(id)};
    if (it == g_preverified.end() || it->second != seed) return false;
    g_preverified.erase(it); // used once: validation stores the header in the index anyway
    return true;
}

void AddPreverified(const uint256& id, const uint256& seed)
{
    std::lock_guard<std::mutex> lock(g_preverified_mutex);
    if (!g_preverified.emplace(id, seed).second) return;
    g_preverified_order.push_back(id);
    while (g_preverified_order.size() > MAX_PREVERIFIED) {
        g_preverified.erase(g_preverified_order.front());
        g_preverified_order.pop_front();
    }
}
// PQCH: header ids whose proof of work is known valid from a checkpoint (AssumeHeaderPow).
constexpr size_t MAX_ASSUMED{50000}; // a redownload buffer (~16,000) and a few batches
std::mutex g_assumed_mutex;
std::unordered_set<uint256, SaltedUint256Hasher> g_assumed;
std::deque<uint256> g_assumed_order;

bool TakeAssumedHeaderPow(const uint256& id)
{
    std::lock_guard<std::mutex> lock(g_assumed_mutex);
    return g_assumed.erase(id) > 0;
}
} // namespace

std::optional<uint256> HeaderCheckpoint(const Consensus::Params& params, int height)
{
    const auto it{params.header_checkpoints.find(height)};
    if (it == params.header_checkpoints.end()) return std::nullopt;
    return it->second;
}

int LastHeaderCheckpointHeight(const Consensus::Params& params)
{
    return params.header_checkpoints.empty() ? 0 : params.header_checkpoints.rbegin()->first;
}

void AssumeHeaderPow(const uint256& id)
{
    std::lock_guard<std::mutex> lock(g_assumed_mutex);
    if (!g_assumed.insert(id).second) return;
    g_assumed_order.push_back(id);
    while (g_assumed_order.size() > MAX_ASSUMED) {
        g_assumed.erase(g_assumed_order.front());
        g_assumed_order.pop_front();
    }
}

bool IsHeaderPowAssumed(const uint256& id)
{
    std::lock_guard<std::mutex> lock(g_assumed_mutex);
    return g_assumed.contains(id);
}

size_t AssumePowBelowCheckpoints(int prev_height, std::span<const CBlockHeader> headers, const Consensus::Params& params)
{
    if (params.header_checkpoints.empty() || prev_height >= LastHeaderCheckpointHeight(params)) return 0;
    std::optional<size_t> last; // index of the highest matching checkpoint in the run
    uint256 prev_id;
    for (size_t i = 0; i < headers.size(); ++i) {
        const uint256 id{headers[i].GetHash()};
        if (i > 0 && headers[i].hashPrevBlock != prev_id) break; // not one chain from here
        prev_id = id;
        const int height{prev_height + 1 + static_cast<int>(i)};
        if (height > LastHeaderCheckpointHeight(params)) break;
        if (const auto cp{HeaderCheckpoint(params, height)}) {
            if (*cp != id) break; // validation rejects it
            last = i;
        }
    }
    if (!last) return 0;
    for (size_t i = 0; i <= *last; ++i) AssumeHeaderPow(headers[i].GetHash());
    return *last + 1;
}

std::vector<uint256> RandomXSeedsForHeaders(int prev_height, const std::function<uint256(int)>& ancestor_hash,
                                            std::span<const CBlockHeader> headers, const Consensus::Params& params)
{
    std::vector<uint256> seeds;
    seeds.reserve(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0 && headers[i].hashPrevBlock != headers[i - 1].GetHash()) break; // not one run
        const int height{prev_height + 1 + static_cast<int>(i)};
        const int seed_height{GetRandomXSeedHeight(height, params)};
        seeds.push_back(seed_height <= prev_height ? ancestor_hash(seed_height) : headers[seed_height - prev_height - 1].GetHash());
    }
    return seeds;
}

size_t PreverifyHeadersPow(std::span<const CBlockHeader> headers, std::span<const uint256> seeds,
                           const Consensus::Params& params, unsigned threads)
{
    const size_t n{std::min(headers.size(), seeds.size())};
    if (!params.fPowUseRandomX || n == 0) return 0;
    threads = std::clamp<unsigned>(threads, 1, 64);
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::atomic<size_t> verified{0};
    auto work = [&] {
        std::shared_ptr<RxContext> ctx;
        randomx_vm* vm{nullptr};
        while (!failed.load()) {
            const size_t i{next.fetch_add(1)};
            if (i >= n) break;
            const CBlockHeader& h{headers[i]};
            if (h.IsAuxPow() || seeds[i].IsNull()) continue; // merged mined: checked in validation
            if (IsHeaderPowAssumed(h.GetHash())) continue;     // below a checkpoint: nothing to hash
            const std::span<const unsigned char> key{seeds[i].data(), seeds[i].size()};
            if (!ctx || !std::ranges::equal(ctx->key, key)) {
                if (vm) randomx_destroy_vm(vm);
                vm = nullptr;
                ctx = GetContext(key); // builds the 256 MiB cache once per key, shared by all threads
                // Each thread its own VM on the shared cache: a VM is not thread-safe, a cache is read-only.
                vm = randomx_create_vm(VmFlags(ctx->jit, /*full_mem=*/false), ctx->cache, nullptr);
                if (!vm) break; // out of memory: validation will hash these itself
            }
            const auto input{SerializeHeader(h)};
            uint256 pow;
            randomx_calculate_hash(vm, input.data(), input.size(), pow.data());
            if (!CheckProofOfWork(pow, h.nBits, params)) {
                failed = true; // invalid from here: validation rejects it, no need to hash the rest
                break;
            }
            AddPreverified(h.GetHash(), seeds[i]);
            ++verified;
        }
        if (vm) randomx_destroy_vm(vm);
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads && t < n; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    return verified.load();
}

uint256 GetRandomXPowHash(const CBlockHeader& header, const uint256& seed)
{
    const auto input{SerializeHeader(header)};
    return RandomXHash(std::span<const unsigned char>{seed.data(), seed.size()}, input);
}

bool CheckBlockProofOfWork(const CBlockHeader& header, const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    std::string error;
    return CheckBlockProofOfWork(header, pindexPrev, params, error);
}

bool CheckHeaderProofOfWork(const CBlockHeader& header, int height, const uint256& seed, const Consensus::Params& params, std::string& error)
{
    if (!params.fPowUseRandomX) {
        if (header.IsAuxPow()) { error = "auxpow not allowed"; return false; }
        if (CheckProofOfWork(header.GetHash(), header.nBits, params)) return true;
        error = "high-hash";
        return false;
    }
    if (header.IsAuxPow()) {
        // PQCH: merged mined with Monero (auxpow.h). Cheap structural checks first, then one RandomX
        // hash of the Monero hashing blob under the Monero key.
        if (height < params.nAuxPowHeight) { error = "auxpow-not-active"; return false; }
        if (!header.auxpow) { error = "auxpow-missing"; return false; }
        std::vector<unsigned char> blob;
        std::string detail;
        if (!auxpow::CheckAuxPowHeader(*header.auxpow, header.GetHash(), params.hashGenesisBlock, blob, detail)) {
            error = "bad-auxpow (" + detail + ")";
            return false;
        }
        if (TakeAssumedHeaderPow(header.GetHash())) return true; // PQCH: below a header checkpoint
        if (TakePreverified(header.GetHash(), header.auxpow->seed_hash)) return true; // PreverifyNewAuxKeyPow
        // Monero's own proof of work: original RandomX before Monero hard fork 17, RandomX v2 from it.
        if (CheckProofOfWork(MoneroPowHash(header.auxpow->seed_hash, blob), header.nBits, params)) return true;
        error = "high-hash";
        return false;
    }
    if (TakeAssumedHeaderPow(header.GetHash())) return true; // PQCH: below a header checkpoint
    if (TakePreverified(header.GetHash(), seed)) return true; // PQCH: already hashed, outside cs_main
    if (CheckProofOfWork(GetRandomXPowHash(header, seed), header.nBits, params)) return true;
    error = "high-hash";
    return false;
}

bool CheckBlockProofOfWork(const CBlockHeader& header, const CBlockIndex* pindexPrev, const Consensus::Params& params, std::string& error)
{
    const bool direct{params.fPowUseRandomX && !header.IsAuxPow()};
    return CheckHeaderProofOfWork(header, pindexPrev->nHeight + 1, direct ? GetRandomXSeedHash(pindexPrev, params) : uint256{}, params, error);
}

bool CheckAuxSeed(const uint256& seed, const CBlockIndex* pindexPrev, const Consensus::Params& params, bool& is_new, std::string& error)
{
    const int height{pindexPrev ? pindexPrev->nHeight + 1 : 0};
    int new_in_window{0};
    const CBlockIndex* p{pindexPrev ? pindexPrev->m_last_new_aux_seed : nullptr};
    // Every link is a block that introduced a key; in a valid chain at most nAuxNewSeedsPerWindow of
    // them are in the window, the bound below only guards against a corrupted index.
    for (int steps{0}; p && p->nHeight > height - params.nAuxSeedWindow && steps <= params.nAuxNewSeedsPerWindow; ++steps) {
        if (p->AuxSeed() == seed) {
            is_new = false;
            return true;
        }
        ++new_in_window;
        p = p->pprev ? p->pprev->m_last_new_aux_seed : nullptr;
    }
    is_new = true;
    if (new_in_window >= params.nAuxNewSeedsPerWindow) {
        error = "auxpow-too-many-seeds";
        return false;
    }
    return true;
}

void SetLastNewAuxSeed(CBlockIndex& pindex, const Consensus::Params& params)
{
    const CBlockIndex* inherited{pindex.pprev ? pindex.pprev->m_last_new_aux_seed : nullptr};
    pindex.m_last_new_aux_seed = inherited;
    if (!pindex.m_auxpow) return;
    bool is_new{false};
    std::string error;
    // A header in the index already passed CheckAuxSeed; only is_new matters here.
    CheckAuxSeed(pindex.m_auxpow->seed_hash, pindex.pprev, params, is_new, error);
    if (is_new) pindex.m_last_new_aux_seed = &pindex;
}

struct RandomXMiner::Impl {
    bool fast;
    uint256 seed;
    bool ready{false};
    randomx_cache* cache{nullptr};
    randomx_dataset* dataset{nullptr};
    randomx_vm* vm{nullptr};

    void Release()
    {
        if (vm) randomx_destroy_vm(vm);
        if (dataset) randomx_release_dataset(dataset);
        if (cache) randomx_release_cache(cache);
        vm = nullptr;
        dataset = nullptr;
        cache = nullptr;
        ready = false;
    }

    void Init(const uint256& new_seed)
    {
        Release();
        const auto start{SteadyClock::now()};
        for (bool jit : {true, false}) {
            cache = randomx_alloc_cache(CacheFlags(jit));
            if (!cache) continue;
            randomx_init_cache(cache, new_seed.data(), new_seed.size());
            dataset = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
            if (!dataset) {
                throw std::runtime_error("RandomX: could not allocate the 2 GiB dataset for fast mining");
            }
            const unsigned long items{randomx_dataset_item_count()};
            const unsigned threads{std::max(1u, std::thread::hardware_concurrency())};
            std::vector<std::thread> workers;
            for (unsigned t = 0; t < threads; ++t) {
                const unsigned long begin{items * t / threads};
                const unsigned long end{items * (t + 1) / threads};
                workers.emplace_back([this, begin, end] { randomx_init_dataset(dataset, cache, begin, end - begin); });
            }
            for (auto& w : workers) w.join();
            vm = randomx_create_vm(VmFlags(jit, /*full_mem=*/true), nullptr, dataset);
            if (vm) break;
            Release();
        }
        if (!vm) throw std::runtime_error("RandomX: could not create the fast-mode virtual machine");
        seed = new_seed;
        ready = true;
        LogInfo("RandomX: fast-mode dataset ready in %d s (seed %s)\n",
                Ticks<std::chrono::seconds>(SteadyClock::now() - start), seed.ToString());
    }
};

namespace {
/** PQCH: one VM per mining thread, over a shared cache (light) or dataset (fast). */
struct SearchVMs {
    randomx_cache* cache{nullptr};
    randomx_dataset* dataset{nullptr};
    std::vector<randomx_vm*> vms;
    uint256 seed;
    bool fast{false};

    void Release()
    {
        for (auto* vm : vms) randomx_destroy_vm(vm);
        vms.clear();
        if (dataset) randomx_release_dataset(dataset);
        if (cache) randomx_release_cache(cache);
        dataset = nullptr;
        cache = nullptr;
    }
    ~SearchVMs() { Release(); }

    void Init(const uint256& new_seed, bool fast_mode, unsigned threads)
    {
        Release();
        const auto start{SteadyClock::now()};
        for (bool jit : {true, false}) {
            cache = randomx_alloc_cache(CacheFlags(jit));
            if (!cache) continue;
            randomx_init_cache(cache, new_seed.data(), new_seed.size());
            if (fast_mode) {
                dataset = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
                if (!dataset) throw std::runtime_error("RandomX: could not allocate the 2 GiB dataset for fast mining");
                const unsigned long items{randomx_dataset_item_count()};
                const unsigned init_threads{std::max(1u, std::thread::hardware_concurrency())};
                std::vector<std::thread> workers;
                for (unsigned t = 0; t < init_threads; ++t) {
                    const unsigned long begin{items * t / init_threads};
                    const unsigned long end{items * (t + 1) / init_threads};
                    workers.emplace_back([this, begin, end] { randomx_init_dataset(dataset, cache, begin, end - begin); });
                }
                for (auto& w : workers) w.join();
            }
            bool ok{true};
            for (unsigned t = 0; t < threads && ok; ++t) {
                randomx_vm* vm{fast_mode ? randomx_create_vm(VmFlags(jit, /*full_mem=*/true), nullptr, dataset)
                                         : randomx_create_vm(VmFlags(jit, /*full_mem=*/false), cache, nullptr)};
                if (vm) vms.push_back(vm); else ok = false;
            }
            if (ok) break;
            Release();
        }
        if (vms.size() != threads) throw std::runtime_error("RandomX: could not create the mining virtual machines");
        seed = new_seed;
        fast = fast_mode;
        LogInfo("RandomX: %s-mode miner ready with %u thread(s) in %d s\n", fast_mode ? "fast" : "light", threads,
                Ticks<std::chrono::seconds>(SteadyClock::now() - start));
    }
};
} // namespace

// PQCH: hash rate of the built-in miner (getmininginfo "localhashps"). Each search adds how many
// hashes it did and for how long (dataset and VM set-up left out); the rate is the total over the
// searches that ended in the last RATE_WINDOW. A steady clock, so -mocktime does not affect it.
namespace {
constexpr auto RATE_WINDOW{std::chrono::minutes{10}};
struct RateSample {
    std::chrono::steady_clock::time_point end;
    double seconds;
    uint64_t hashes;
};
Mutex g_rate_mutex;
std::deque<RateSample> g_rate_samples GUARDED_BY(g_rate_mutex);

void PruneRate(std::chrono::steady_clock::time_point now) EXCLUSIVE_LOCKS_REQUIRED(g_rate_mutex)
{
    while (!g_rate_samples.empty() && now - g_rate_samples.front().end > RATE_WINDOW) g_rate_samples.pop_front();
}

void RecordRate(double seconds, uint64_t hashes)
{
    if (hashes == 0 || seconds <= 0) return;
    const auto now{std::chrono::steady_clock::now()};
    LOCK(g_rate_mutex);
    PruneRate(now);
    g_rate_samples.push_back({now, seconds, hashes});
}
} // namespace

double RandomXMiningHashrate()
{
    LOCK(g_rate_mutex);
    PruneRate(std::chrono::steady_clock::now());
    double seconds{0};
    uint64_t hashes{0};
    for (const auto& s : g_rate_samples) {
        seconds += s.seconds;
        hashes += s.hashes;
    }
    return seconds > 0 ? hashes / seconds : 0;
}

static Mutex g_search_mutex;
static SearchVMs g_search_vms GUARDED_BY(g_search_mutex);

bool RandomXMiner::Search(CBlockHeader& header, const uint256& seed, unsigned threads, uint64_t& max_tries,
                          const std::function<bool(const uint256&)>& ok, const std::function<bool()>& interrupted)
{
    threads = std::max(1u, threads);
    LOCK(g_search_mutex);
    if (g_search_vms.vms.size() != threads || g_search_vms.seed != seed || g_search_vms.fast != m_impl->fast) {
        m_impl->Release(); // never two fast-mode datasets in memory at once
        g_search_vms.Init(seed, m_impl->fast, threads);
    }
    const uint32_t first{header.nNonce};
    const uint64_t room{uint64_t{std::numeric_limits<uint32_t>::max()} - first};
    const uint64_t budget{std::min<uint64_t>(max_tries, room)};
    std::atomic<uint64_t> done{0};
    std::atomic<bool> found{false};
    std::atomic<uint32_t> winner{0};
    std::vector<std::thread> workers;
    const auto start{std::chrono::steady_clock::now()};
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            CBlockHeader h{header};
            randomx_vm* vm{g_search_vms.vms[t]};
            uint256 out;
            for (uint64_t i = t; i < budget && !found.load(std::memory_order_relaxed) && !(i % 64 < threads && interrupted()); i += threads) {
                h.nNonce = first + static_cast<uint32_t>(i);
                const auto input{SerializeHeader(h)};
                randomx_calculate_hash(vm, input.data(), input.size(), out.data());
                done.fetch_add(1, std::memory_order_relaxed);
                if (ok(out) && !found.exchange(true)) {
                    winner = h.nNonce;
                }
            }
        });
    }
    for (auto& w : workers) w.join();
    RecordRate(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), done.load());
    max_tries -= std::min<uint64_t>(max_tries, done.load());
    if (found) {
        header.nNonce = winner;
        return true;
    }
    header.nNonce = first + static_cast<uint32_t>(std::min(budget, room));
    return false;
}

RandomXMiner::RandomXMiner(bool fast_mode) : m_impl{std::make_unique<Impl>()}
{
    m_impl->fast = fast_mode;
}

RandomXMiner::~RandomXMiner()
{
    m_impl->Release();
}


uint256 RandomXMiner::Hash(const CBlockHeader& header, const uint256& seed)
{
    if (!m_impl->fast) return GetRandomXPowHash(header, seed);
    if (!m_impl->ready || m_impl->seed != seed) m_impl->Init(seed);
    const auto input{SerializeHeader(header)};
    uint256 out;
    randomx_calculate_hash(m_impl->vm, input.data(), input.size(), out.data());
    return out;
}

std::optional<bool> PreverifyNewAuxKeyPow(const CBlockHeader& header, const Consensus::Params& params)
{
    if (!params.fPowUseRandomX || !header.IsAuxPow() || !header.auxpow) return false;
    std::vector<unsigned char> blob;
    std::string detail;
    if (!auxpow::CheckAuxPowHeader(*header.auxpow, header.GetHash(), params.hashGenesisBlock, blob, detail)) return false;
    const uint256& seed{header.auxpow->seed_hash};
    const std::span<const unsigned char> key{seed.data(), seed.size()};
    // A cache of its own (~0.5 s, 256 MiB), outside the shared list: a key with fake work never
    // takes the place of a cache in use.
    std::shared_ptr<RxContext> ctx;
    try {
        ctx = std::make_shared<RxContext>(key);
    } catch (const std::exception& e) {
        LogWarning("RandomX: %s\n", e.what());
        return std::nullopt;
    }
    const bool v2{!blob.empty() && blob[0] >= MONERO_HF_RANDOMX_V2};
    uint256 hash;
    {
        std::lock_guard<std::mutex> lock(ctx->vm_mutex);
        randomx_calculate_hash(v2 ? ctx->vm : ctx->vm_v1, blob.data(), blob.size(), hash.data());
    }
    if (v2) hash = RandomXCommitment(blob, hash);
    if (!CheckProofOfWork(hash, header.nBits, params)) return false;
    // Real work: the key is one Monero miners use. Validation takes the result instead of hashing
    // again, and the cache joins the shared list for the blocks that follow under the same key.
    AddPreverified(header.GetHash(), seed);
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    if (std::none_of(g_rx_contexts.begin(), g_rx_contexts.end(), [&](const auto& c) { return std::ranges::equal(c->key, key); })) {
        g_rx_contexts.push_front(ctx);
        while (g_rx_contexts.size() > MAX_RX_CONTEXTS) g_rx_contexts.pop_back();
    }
    return true;
}
