cmake_minimum_required(VERSION 3.20)
project(ecs_spike CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Spike library ─────────────────────────────────────────────────────────────

add_library(ecs_spike_lib STATIC
    src/CommandBuffer.cpp
)

target_include_directories(ecs_spike_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(ecs_spike_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -march=native -DNDEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /DNDEBUG>
)

# ── Benchmarks ────────────────────────────────────────────────────────────────

add_executable(bench_iteration  benchmark/bench_iteration.cpp)
add_executable(bench_churn      benchmark/bench_churn.cpp)

foreach(target bench_iteration bench_churn)
    target_link_libraries(${target} PRIVATE ecs_spike_lib)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -march=native -DNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /DNDEBUG>
    )
endforeach()

# ── Examples ─────────────────────────────────────────────────────────────────

add_executable(examples  examples/example_systems.cpp)
target_link_libraries(examples PRIVATE ecs_spike_lib)
