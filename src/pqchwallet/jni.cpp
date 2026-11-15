// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// PQCH: the Android (JNI) side of the light wallet core: org.pqch.wallet.Core.call(method, params).

#include <pqchwallet/pqchwallet.h>

#include <jni.h>

#include <cstring>

extern "C" JNIEXPORT jstring JNICALL Java_org_pqch_wallet_Core_call(JNIEnv* env, jclass, jstring method, jstring params)
{
    const char* m{method ? env->GetStringUTFChars(method, nullptr) : nullptr};
    const char* p{params ? env->GetStringUTFChars(params, nullptr) : nullptr};
    char* out{pqchw_call(m, p)};
    if (p) {
        // The parameters may hold the recovery words: wipe JNI's copy before giving it back.
        std::memset(const_cast<char*>(p), 0, std::strlen(p));
        env->ReleaseStringUTFChars(params, p);
    }
    if (m) env->ReleaseStringUTFChars(method, m);
    jstring result{env->NewStringUTF(out ? out : "{\"ok\":false,\"error\":\"out of memory\"}")};
    pqchw_free(out);
    return result;
}
