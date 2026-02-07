file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/src/*.cpp
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include/*.h
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)
