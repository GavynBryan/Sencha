#pragma once

#include "operations/EditorOperations.h"

#include <zone/WorldPartitionIds.h>

#include <optional>
#include <string_view>
#include <vector>

class WorldDocument;
struct WorldPartitionManifest;

// A zone named by its id, or failing that by its name.
[[nodiscard]] std::optional<ZoneId> ZoneNamed(const WorldPartitionManifest& manifest, std::string_view text);

// world.focus: which document of a world the level operations read and write,
// a zone or the world scene that holds the docks. `world` must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> WorldFocusOperations(WorldDocument& world);
