# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(bitcoin_crypto STATIC EXCLUDE_FROM_ALL
  $<$<NOT:$<STREQUAL:${CMAKE_SYSTEM_NAME},Generic>>:aes.cpp>
  chacha20.cpp
  chacha20poly1305.cpp
  hex_base.cpp
  hkdf_sha256_32.cpp
  hmac_sha256.cpp
  hmac_sha512.cpp
  muhash.cpp
  poly1305.cpp
  ripemd160.cpp
  sha1.cpp
  sha256.cpp
  sha256_sse4.cpp
  sha3.cpp
  sha512.cpp
  siphash.cpp
  ../support/cleanse.cpp
  $<$<NOT:$<STREQUAL:${CMAKE_SYSTEM_NAME},Generic>>:../support/lockedpool.cpp>
)

target_link_libraries(bitcoin_crypto
  PRIVATE
    core_interface
)

if(HAVE_SSE41)
  target_compile_definitions(bitcoin_crypto PRIVATE ENABLE_SSE41)
  target_sources(bitcoin_crypto PRIVATE sha256_sse41.cpp)
  set_property(SOURCE sha256_sse41.cpp PROPERTY
    COMPILE_OPTIONS ${SSE41_CXXFLAGS}
  )
endif()

if(HAVE_AVX2)
  target_compile_definitions(bitcoin_crypto PRIVATE ENABLE_AVX2)
  target_sources(bitcoin_crypto PRIVATE sha256_avx2.cpp)
  set_property(SOURCE sha256_avx2.cpp PROPERTY
    COMPILE_OPTIONS ${AVX2_CXXFLAGS}
  )
endif()

if(HAVE_SSE41 AND HAVE_X86_SHANI)
  target_compile_definitions(bitcoin_crypto PRIVATE ENABLE_SSE41 ENABLE_X86_SHANI)
  target_sources(bitcoin_crypto PRIVATE sha256_x86_shani.cpp)
  set_property(SOURCE sha256_x86_shani.cpp PROPERTY
    COMPILE_OPTIONS ${SSE41_CXXFLAGS} ${X86_SHANI_CXXFLAGS}
  )
endif()

if(HAVE_ARM_SHANI)
  target_compile_definitions(bitcoin_crypto PRIVATE ENABLE_ARM_SHANI)
  target_sources(bitcoin_crypto PRIVATE sha256_arm_shani.cpp)
  set_property(SOURCE sha256_arm_shani.cpp PROPERTY
    COMPILE_OPTIONS ${ARM_SHANI_CXXFLAGS}
  )
endif()

# PQCH: post-quantum signatures (whitepaper, section 21). Third-party C code, built as is in its
# own library so that the project's warning flags do not apply to it:
#  - mldsa-native v2.0.0 (ML-DSA, FIPS 204), parameter set 44, without the randomized API
#    (PQCH supplies the randomness itself), symbols prefixed PQCH_mldsa44_.
#  - slhdsa-c (SLH-DSA, FIPS 205).
enable_language(C)
add_library(pqch_pqc_c STATIC EXCLUDE_FROM_ALL
  pqc/mldsa-native/mldsa_native.c
  pqc/slhdsa-c/sha2_256.c
  pqc/slhdsa-c/sha2_512.c
  pqc/slhdsa-c/sha3_api.c
  pqc/slhdsa-c/sha3_f1600.c
  pqc/slhdsa-c/slh_dsa.c
  pqc/slhdsa-c/slh_prehash.c
  pqc/slhdsa-c/slh_sha2.c
  pqc/slhdsa-c/slh_shake.c
)
set_target_properties(pqch_pqc_c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(pqch_pqc_c
  PUBLIC
    MLD_CONFIG_PARAMETER_SET=44
    MLD_CONFIG_NAMESPACE_PREFIX=PQCH_mldsa44
    MLD_CONFIG_NO_RANDOMIZED_API
)
target_include_directories(pqch_pqc_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pqc/mldsa-native)
target_compile_options(pqch_pqc_c PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-O2 -w>)

target_sources(bitcoin_crypto PRIVATE pqc/pqc.cpp)
target_link_libraries(bitcoin_crypto PUBLIC pqch_pqc_c)
