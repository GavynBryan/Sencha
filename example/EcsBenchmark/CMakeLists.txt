add_executable(EcsBenchmark
    EcsBenchmark.cpp
)

target_link_libraries(EcsBenchmark PRIVATE engine)

target_compile_features(EcsBenchmark PRIVATE cxx_std_20)
