add_executable(AudioTest
    main.cpp
)

target_link_libraries(AudioTest PRIVATE engine)

# Source audio (WAV) is committed under assets/ and cooked to .sclip by
# import-on-demand at startup (docs/assets/pipeline.md, Stage 4d).
add_custom_command(TARGET AudioTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:AudioTest>/assets
)
