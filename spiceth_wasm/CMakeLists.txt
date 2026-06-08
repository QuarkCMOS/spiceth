cmake_minimum_required(VERSION 3.20)
project(CircuitEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Eigen (header-only) ───────────────────────────────────────────────────────
# Option A: system Eigen via vcpkg / apt / brew
find_package(Eigen3 QUIET)

if(NOT Eigen3_FOUND)
    # Option B: fetch Eigen automatically if not found
    include(FetchContent)
    FetchContent_Declare(
        Eigen3
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
    )
    set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Eigen3)
endif()

# ── WASM target ───────────────────────────────────────────────────────────────
add_executable(circuit_engine circuit_wasm.cpp)

target_include_directories(circuit_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(circuit_engine PRIVATE Eigen3::Eigen)

# Emscripten-specific flags
if(EMSCRIPTEN)
    target_compile_options(circuit_engine PRIVATE
        -O3
        -fno-exceptions-unwind-tables   # smaller binary
    )

    target_link_options(circuit_engine PRIVATE
        # Output
        "SHELL:-s WASM=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME=CircuitEngineModule"

        # Exports needed by Embind + cwrap path
        "SHELL:-s EXPORTED_FUNCTIONS=[\"_simulate_json\",\"_malloc\",\"_free\"]"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"]"

        # Memory
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=33554432"   # 32 MB

        # No POSIX environment
        "SHELL:-s NO_EXIT_RUNTIME=1"
        "SHELL:-s FILESYSTEM=0"              # no MEMFS / NODEFS

        # Embind
        -lembind

        # Optional: generate TypeScript type declarations
        # "--emit-tsd circuit_engine.d.ts"
    )

    # Output files go next to the JS bundle
    set_target_properties(circuit_engine PROPERTIES
        OUTPUT_NAME "circuit_engine"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    )
endif()
