add_executable(JobSystemBenchmark
    JobSystemBenchmark.cpp
)

target_link_libraries(JobSystemBenchmark PRIVATE engine)

target_compile_features(JobSystemBenchmark PRIVATE cxx_std_20)
