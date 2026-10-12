#pragma once

#include <core/identity/Id.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

// The world scene, as opposed to one of its zones.
struct WorldScenePlace
{
    friend bool operator==(WorldScenePlace, WorldScenePlace) = default;
};

// Where something in a level is, as the hierarchy, the breadcrumb and a problem
// name it: the world scene, a graph, a zone, a transition, or an entity by its
// persistent id.
using LevelLocation = std::variant<WorldScenePlace, GraphId, ZoneId, TransitionId, PersistentEntityId>;

// "world", "graph:<id>", "zone:<id>", "transition:<id>", "entity:<id>".
[[nodiscard]] std::string LevelLocationKey(const LevelLocation& location);
[[nodiscard]] std::optional<LevelLocation> ParseLevelLocation(std::string_view key);
