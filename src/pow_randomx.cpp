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
#include <util/time.h>

#include <randomx.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
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

/** Flags for a virtual machine running RandomX v2. */
randomx_flags VmFlags(bool allow_jit, bool full_mem)
{
    int f = CacheFlags(allow_jit) | RANDOMX_FLAG_V2;
    if (full_mem) f |= RANDOMX_FLAG_FULL_MEM;
    return randomx_flags(f);
}

/** A light-mode cache and VM for one key. The VM is not thread-safe, hence the mutex. */
struct RxContext {
    std::vector<unsigned char> key;
    randomx_cache* cache{nullptr};
    randomx_vm* vm{nullptr};
    std::mutex vm_mutex;

    explicit RxContext(std::span<const unsigned char> k) : key(k.begin(), k.end())
    {
        for (bool jit : {true, false}) {
            cache = randomx_alloc_cache(CacheFlags(jit));
            if (!cache) continue;
            randomx_init_cache(cache, key.data(), key.size());
            vm = randomx_create_vm(VmFlags(jit, /*full_mem=*/false), cache, nullptr);
            if (vm) return;
            randomx_release_cache(cache);
            cache = nullptr;
        }
        throw std::runtime_error("RandomX: could not allocate cache/VM (256 MiB of memory are required)");
    }
    ~RxContext()
    {
        if (vm) randomx_destroy_vm(vm);
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

uint256 RandomXHash(std::span<const unsigned char> key, std::span<const unsigned char> input)
{
    auto ctx{GetContext(key)};
    uint256 out;
    std::lock_guard<std::mutex> lock(ctx->vm_mutex);
    randomx_calculate_hash(ctx->vm, input.data(), input.size(), out.data());
    return out;
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
        const uint256& aux_seed{header.auxpow->seed_hash};
        if (CheckProofOfWork(RandomXHash(std::span<const unsigned char>{aux_seed.data(), aux_seed.size()}, blob), header.nBits, params)) return true;
        error = "high-hash";
        return false;
    }
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
