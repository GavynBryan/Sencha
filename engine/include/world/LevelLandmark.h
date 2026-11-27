#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <optional>
#include <string_view>

class World;

// A named place in a level where a body arrives: docs/gameplay/game-module.md, "Arrivals".
struct SENCHA_COMPONENT("sencha.level_landmark")
       SENCHA_SCHEMA("LevelLandmark")
       SENCHA_SCENE_CHUNK("LVLM")
       SENCHA_PLACEABLE("Level Landmark")
       SENCHA_PLACE_ICON("flag")
LevelLandmark
{
    SENCHA_FIELD("name")
    SENCHA_TOOLTIP("What a level change or a save names to bring a body here.")
    InlineString<64> Name;
};

struct LevelLandmarkPlace
{
    EntityId Entity;
    // Where a body arriving here stands: the landmark's position and facing in the world, unscaled.
    Transform3f Place = Transform3f::Identity();
};

// The loaded landmark of that name; of two, the one with the lowest persistent id.
[[nodiscard]] std::optional<LevelLandmarkPlace> FindLevelLandmark(const World& world, std::string_view name);

#if !defined(SENCHA_CODEGEN)
#  include <world/LevelLandmark.sencha.h>
#endif
