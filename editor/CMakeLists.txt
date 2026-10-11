if(NOT SENCHA_ENABLE_VULKAN)
    message(STATUS "Skipping editor applications: require SENCHA_ENABLE_VULKAN=ON")
    return()
endif()

# Kyusu and its workspaces. editor_common is what every workspace shares (the
# shell, actions, properties, input, commands, selection, tools, project
# descriptor); each workspace library links it and adds its own domain code.
add_subdirectory(common)
add_subdirectory(level_editor)
add_subdirectory(kyusu)
add_subdirectory(kyusu_batch)
add_subdirectory(material_editor)
add_subdirectory(ui_preview)
add_subdirectory(data_editor)
add_subdirectory(animation_editor)
add_subdirectory(ability_editor)
add_subdirectory(project_browser)
