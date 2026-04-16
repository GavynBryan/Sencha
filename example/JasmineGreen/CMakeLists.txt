add_executable(JasmineGreen
    main.cpp
    PlayerSystem.cpp
)

target_link_libraries(JasmineGreen PRIVATE engine)

# Copy the input binding config next to the executable so it can be loaded
# at runtime via a relative path.
configure_file(input_config.json input_config.json COPYONLY)
