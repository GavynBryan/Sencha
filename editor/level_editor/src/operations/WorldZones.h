#pragma once

#include "operations/EditorOperations.h"

#include <core/metadata/DataSchema.h>

#include <zone/WorldPartitionIds.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class WorldDocument;
struct WorldPartitionManifest;

// A zone named by its id, or failing that by the name it shows.
[[nodiscard]] std::optional<ZoneId> ZoneNamed(const WorldPartitionManifest& manifest, std::string_view text);

// {id, name}, the name as the world shows it.
[[nodiscard]] JsonValue ZoneJson(const WorldPartitionManifest& manifest, ZoneId zone);
[[nodiscard]] DataFieldSchema ZoneSchema(std::string key);

// world.focus: which document of a world the level operations read and write,
// a zone or the world scene that holds the docks. `world` must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> WorldFocusOperations(WorldDocument& world);
