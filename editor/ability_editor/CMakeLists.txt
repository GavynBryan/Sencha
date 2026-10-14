add_library(ability_authoring STATIC
    src/authoring/AbilityArguments.cpp
    src/authoring/AbilityChecks.cpp
    src/authoring/AbilityConditionText.cpp
    src/authoring/AbilityDocumentEdits.cpp
    src/authoring/AbilityElements.cpp
    src/authoring/AbilityExplorers.cpp
    src/authoring/AbilityGuardFixes.cpp
    src/authoring/AbilityOverview.cpp
    src/authoring/AbilityPalette.cpp
    src/authoring/AbilityProblemProperties.cpp
    src/authoring/AbilityRecipes.cpp
    src/authoring/AbilityStrip.cpp
    src/authoring/AbilityWorkbench.cpp)
target_link_libraries(ability_authoring PUBLIC editor_common)
target_include_directories(ability_authoring PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
sencha_warnings(ability_authoring)

# The Ability Workbench's shell, linked by the application that hosts it.
add_library(ability_editor STATIC
    src/AbilityPhaseStrip.cpp
    src/AbilityWorkspace.cpp)
target_link_libraries(ability_editor PUBLIC ability_authoring editor_common)
sencha_warnings(ability_editor)
