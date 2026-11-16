#pragma once

#include "scene_source/Json5Value.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct DataFieldSchema;

// How a component's scene form composes, by its JSON key; null for one that
// declares nothing (IComponentSerializer::CompositionSchema).
using SceneComponentSchemaFn = std::function<const DataFieldSchema*(std::string_view component)>;

// How an entity's live components differ from what it inherits.
struct SceneComponentDiff
{
    // Changed members only; a keyed list as { key: patch }, a removed element null.
    Json5Value Patch = Json5Value::MakeObject();
    // Whole components the baseline lacks.
    Json5Value Added = Json5Value::MakeObject();
    std::vector<std::string> Removed;
};

// Brush geometry names ids in its own document's sidecar, so it never crosses
// documents through a diff.
[[nodiscard]] bool IsDocumentLocalComponent(std::string_view component);

// The sparsest difference that composes back to `live` over `baseline`, both in
// the shape serializers write.
[[nodiscard]] SceneComponentDiff DiffSceneComponents(const Json5Value& live, const Json5Value& baseline,
                                                     const SceneComponentSchemaFn& schemaOf);
