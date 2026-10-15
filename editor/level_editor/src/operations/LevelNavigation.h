#pragma once

#include "operations/EditorOperations.h"

#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavigationFile.h>
#include <navigation/NavigationTypes.h>
#include <navigation/ZoneNavigation.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class EditorDocument;

// A level's navigation built in memory and loaded for questions, under a
// scenario: the traversal kinds held, denied, and left unknown
// (docs/plans/level-spatial-model.md, section 6.4). Every question is asked
// twice: the sure walk opens only what is held, the possible walk every
// unknown kind too.
struct LevelNavigation
{
    GameplayTagRegistry Tags;
    NavigationFile File;
    ZoneNavigation Navigation;
    ZoneId Zone{ 1 };
    std::string Profile;
    std::uint16_t ProfileIndex = 0;

    std::vector<std::string> Held;
    std::vector<std::string> Denied;
    std::vector<std::string> Unknown;
    std::vector<GameplayTagId> Sure;
    std::vector<GameplayTagId> Possible;

    // Holds `capabilities` by reference: it must outlive the request.
    [[nodiscard]] NavQueryRequest Request(std::span<const GameplayTagId> capabilities) const;
    [[nodiscard]] JsonValue AssumptionsJson() const;
};

// The parameters every scenario question takes: held, denied and profile.
[[nodiscard]] std::vector<DataFieldSchema> ScenarioParameters();
[[nodiscard]] DataFieldSchema AssumptionsSchema();

// Null, with `refusal` set, when the level has no navigation (no project
// settings, no walkable geometry, a build error) or the scenario is malformed.
[[nodiscard]] std::unique_ptr<LevelNavigation> BuildLevelNavigation(const EditorDocument& document,
                                                                    const JsonValue& arguments,
                                                                    EditorOperationOutcome& refusal);
