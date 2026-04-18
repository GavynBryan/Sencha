set(SENCHA_RUNTIME_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_RUNTIME_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_RUNTIME_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_RUNTIME_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_RUNTIME_INCLUDE_DIR}/*.h"
)

add_library(sencha_runtime STATIC
    ${SENCHA_RUNTIME_SOURCES}
    ${SENCHA_RUNTIME_HEADERS}
)
add_library(sencha::runtime ALIAS sencha_runtime)

target_include_directories(sencha_runtime
    PUBLIC "${SENCHA_RUNTIME_INCLUDE_DIR}"
)
target_link_libraries(sencha_runtime PUBLIC sencha_core)
