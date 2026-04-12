add_executable(SenchaInputSystem
    SenchaInputSystem.cpp
)

target_link_libraries(SenchaInputSystem PRIVATE engine)

target_compile_definitions(SenchaInputSystem PRIVATE
    SENCHA_INPUT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/example/input_config.json"
)
