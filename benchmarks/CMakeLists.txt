# Benchmark instrumentation. Built only with -DHYPERFINDER_BUILD_BENCHMARKS=ON.

add_executable(walk_bench tools/walk_bench.cpp)
target_link_libraries(walk_bench PRIVATE hyperfinder::core hyperfinder_runtime)
hyperfinder_set_warnings(walk_bench)

# The baseline is the honest reference the benchmarks compare against: one
# thread, std::filesystem only, no project code at all.
add_executable(baseline_walk tools/baseline_walk.cpp)
target_link_libraries(baseline_walk PRIVATE hyperfinder_runtime)
hyperfinder_set_warnings(baseline_walk)
