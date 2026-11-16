#pragma once

#include "scene_source/Json5Value.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct DataFieldSchema;

using SceneComponentSchemaFn = std::function<const DataFieldSchema*(std::string_view component)>;

struct SceneComponentDiff
{
    Json5Value Patch = Json5Value::MakeObject();
    Json5Value Added = Json5Value::MakeObject();
    std::vector<std::string> Removed;
};

[[nodiscard]] bool IsDocumentLocalComponent(std::string_view component);

[[nodiscard]] SceneComponentDiff DiffSceneComponents(const Json5Value& live, const Json5Value& baseline,
                                                     const SceneComponentSchemaFn& schemaOf);
