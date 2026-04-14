add_executable(QuadTreeDemo
    QuadTreeDemo.cpp
)

target_link_libraries(QuadTreeDemo PRIVATE engine)

target_compile_definitions(QuadTreeDemo PRIVATE
    SENCHA_QUADTREE_INPUT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/input_config.json"
)
