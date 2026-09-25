#pragma once

#include "authoring/AnimationRigRecipe.h"

#include <core/json/JsonValue.h>

#include <filesystem>
#include <string>
#include <vector>

class AnimationClipCache;

// Scenes naming the retired AnimationClipPlayer, and one-layer rigs that play
// each clip as its player did, under animation/migrated/.

struct AnimationClipPlayerUse
{
    // Relative to the content root.
    std::string Scene;
    std::string Entity;
    std::string Clip;
    double TimeSeconds = 0.0;
    double Rate = 1.0;
    bool Loop = true;
};

// Every entity in a .sscene under `root` that names the player, in scene then
// entity order. `problems` gets one line per scene that does not parse.
[[nodiscard]] std::vector<AnimationClipPlayerUse> FindAnimationClipPlayers(const std::filesystem::path& root,
                                                                           std::vector<std::string>& problems);

struct AnimationClipPlayerMigrationPlan
{
    std::string Error;
    std::vector<AnimationNewDocument> Documents;
    std::vector<AnimationNewDocument> Scenes;
};

[[nodiscard]] AnimationClipPlayerMigrationPlan PlanAnimationClipPlayerMigration(
    const std::filesystem::path& root, const std::vector<AnimationClipPlayerUse>& uses, const AnimationClipCache& clips);
