add_library(sencha_3d INTERFACE)
add_library(sencha::3d ALIAS sencha_3d)

target_include_directories(sencha_3d
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(sencha_3d INTERFACE sencha_core)
