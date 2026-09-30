if(NOT SENCHA_ENABLE_VULKAN)
    message(STATUS "Skipping editor applications: require SENCHA_ENABLE_VULKAN=ON")
    return()
endif()

# Editor application family. editor_common is the shared editor shell (ImGui
# feature, theme/skin, input, commands, selection, tools, project descriptor);
# each application links it and adds its own domain code.
add_subdirectory(common)
add_subdirectory(level_editor)
add_subdirectory(kyusu)
add_subdirectory(material_editor)
add_subdirectory(ui_preview)
add_subdirectory(data_editor)
add_subdirectory(animation_editor)
add_subdirectory(kettle)
