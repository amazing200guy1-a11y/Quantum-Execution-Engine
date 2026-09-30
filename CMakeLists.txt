cmake_minimum_required(VERSION 3.20)
project(QuantumRiskKernel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Release build flags: maximum optimization + SIMD ──────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
    add_compile_options(-O3 -march=native -funroll-loops -ffast-math)
    message(STATUS "Build: Release (-O3 -march=native)")
endif()

# ── AVX2 detection ─────────────────────────────────────────────────────────────
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" HAS_AVX2)
if(HAS_AVX2)
    add_compile_options(-mavx2)
    add_compile_definitions(QUANTUM_HAS_AVX2)
    message(STATUS "SIMD: AVX2 enabled")
else()
    message(STATUS "SIMD: AVX2 not available — using scalar fallback")
endif()

# ── Address Sanitizer (Debug only) ─────────────────────────────────────────────
option(QUANTUM_ASAN "Enable AddressSanitizer" OFF)
if(QUANTUM_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers: ASan + UBSan ON")
endif()

# ── Library target ─────────────────────────────────────────────────────────────
add_library(risk_kernel STATIC
    src/risk_kernel.cpp
)
target_include_directories(risk_kernel PUBLIC include)

# ── Benchmark executable ────────────────────────────────────────────────────────
add_executable(bench_risk
    benchmarks/bench_risk_kernel.cpp
)
target_link_libraries(bench_risk PRIVATE risk_kernel)
target_include_directories(bench_risk PRIVATE include)

# ── Unit test executable ────────────────────────────────────────────────────────
add_executable(test_risk
    tests/test_risk_kernel.cpp
)
target_link_libraries(test_risk PRIVATE risk_kernel)
target_include_directories(test_risk PRIVATE include)

# ── CTest integration ──────────────────────────────────────────────────────────
enable_testing()
add_test(NAME RiskKernelTests COMMAND test_risk)
