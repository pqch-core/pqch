// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: tiny C interface to RandomX v2 for the Python functional test framework
// (test/functional/test_framework/randomx.py loads it with ctypes to mine blocks it builds itself).
// Light mode, one cached key. Test use only.

#include <randomx.h>

#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

namespace {
std::mutex g_mutex;
std::vector<unsigned char> g_key;
randomx_cache* g_cache{nullptr};
randomx_vm* g_vm{nullptr};

bool Setup(const unsigned char* key, size_t key_len)
{
    if (g_vm && g_key.size() == key_len && std::memcmp(g_key.data(), key, key_len) == 0) return true;
    if (g_vm) randomx_destroy_vm(g_vm);
    if (g_cache) randomx_release_cache(g_cache);
    g_vm = nullptr;
    g_cache = nullptr;
    for (bool jit : {true, false}) {
        int flags = randomx_get_flags() & ~(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_LARGE_PAGES | RANDOMX_FLAG_V2);
        if (!jit) flags &= ~(RANDOMX_FLAG_JIT | RANDOMX_FLAG_SECURE);
        g_cache = randomx_alloc_cache(randomx_flags(flags));
        if (!g_cache) continue;
        randomx_init_cache(g_cache, key, key_len);
        g_vm = randomx_create_vm(randomx_flags(flags | RANDOMX_FLAG_V2), g_cache, nullptr);
        if (g_vm) break;
        randomx_release_cache(g_cache);
        g_cache = nullptr;
    }
    if (!g_vm) return false;
    g_key.assign(key, key + key_len);
    return true;
}
} // namespace

extern "C" int pqch_randomx_hash(const unsigned char* key, size_t key_len, const unsigned char* input, size_t input_len, unsigned char* out32)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!Setup(key, key_len)) return 0;
    randomx_calculate_hash(g_vm, input, input_len, out32);
    return 1;
}
