# Copyright (c) 2026 The PQCH developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# PQCH: builds the bundled RandomX library (src/randomx, BSD-3-Clause) as a static
# library. Unlike RandomX's own CMakeLists.txt, this never touches the project's
# global compiler flags: every option is scoped to the randomx target.

# The JIT trampolines are GNU assembler sources (.S, preprocessed). They must be built as ASM:
# building them as C (as RandomX's own CMakeLists does under old CMake policies) fails with
# CMP0119, which passes "-x c" to the compiler.
if(NOT MSVC)
  enable_language(ASM)
endif()

function(add_randomx subdir)
  set(RX ${PROJECT_SOURCE_DIR}/src/${subdir}/src)
  set(randomx_sources
    ${RX}/aes_hash.cpp
    ${RX}/argon2_ref.c
    ${RX}/argon2_ssse3.c
    ${RX}/argon2_avx2.c
    ${RX}/bytecode_machine.cpp
    ${RX}/cpu.cpp
    ${RX}/dataset.cpp
    ${RX}/soft_aes.cpp
    ${RX}/virtual_memory.c
    ${RX}/vm_interpreted.cpp
    ${RX}/allocator.cpp
    ${RX}/assembly_generator_x86.cpp
    ${RX}/instruction.cpp
    ${RX}/randomx.cpp
    ${RX}/superscalar.cpp
    ${RX}/vm_compiled.cpp
    ${RX}/vm_interpreted_light.cpp
    ${RX}/argon2_core.c
    ${RX}/blake2_generator.cpp
    ${RX}/instructions_portable.cpp
    ${RX}/reciprocal.c
    ${RX}/virtual_machine.cpp
    ${RX}/vm_compiled_light.cpp
    ${RX}/blake2/blake2b.c
  )

  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" rx_arch)
  set(rx_x86_64 FALSE)
  set(rx_arm64 FALSE)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND rx_arch MATCHES "^(x86_64|x86-64|amd64)$")
    set(rx_x86_64 TRUE)
    list(APPEND randomx_sources ${RX}/jit_compiler_x86.cpp ${RX}/jit_compiler_x86_static.S)
    set_property(SOURCE ${RX}/jit_compiler_x86_static.S PROPERTY LANGUAGE ASM)
  elseif(rx_arch MATCHES "^(aarch64|arm64|armv8-a)$")
    set(rx_arm64 TRUE)
    list(APPEND randomx_sources ${RX}/jit_compiler_a64.cpp ${RX}/jit_compiler_a64_static.S)
    set_property(SOURCE ${RX}/jit_compiler_a64_static.S PROPERTY LANGUAGE ASM)
  endif()
  # Other architectures use the portable interpreter (no JIT).

  add_library(randomx STATIC EXCLUDE_FROM_ALL ${randomx_sources})
  target_include_directories(randomx PUBLIC $<BUILD_INTERFACE:${RX}>)
  set_target_properties(randomx PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(randomx PRIVATE NDEBUG)

  include(CheckCCompilerFlag)
  if(rx_x86_64 AND NOT MSVC)
    # Hardware AES is compiled in; software AES is still selected at runtime on CPUs without it.
    target_compile_options(randomx PRIVATE -maes)
    check_c_compiler_flag(-mssse3 RANDOMX_HAVE_SSSE3)
    if(RANDOMX_HAVE_SSSE3)
      set_source_files_properties(${RX}/argon2_ssse3.c PROPERTIES COMPILE_OPTIONS -mssse3)
    endif()
    check_c_compiler_flag(-mavx2 RANDOMX_HAVE_AVX2)
    if(RANDOMX_HAVE_AVX2)
      set_source_files_properties(${RX}/argon2_avx2.c PROPERTIES COMPILE_OPTIONS -mavx2)
    endif()
  elseif(rx_arm64 AND NOT MSVC)
    target_compile_options(randomx PRIVATE -march=armv8-a+crypto)
  endif()

  find_package(Threads REQUIRED)
  target_link_libraries(randomx PRIVATE Threads::Threads)
endfunction()
