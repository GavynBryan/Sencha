#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <ecs/EntityId.h>

#include <span>
#include <string>
#include <string_view>

class EditorDocument;
class EditorScene;
struct SelectableRef;

// How the level's operations name entities in their answers.

// space, opening, wall, volume, brush or entity.
[[nodiscard]] std::string_view EntityKind(const EditorScene& scene, EntityId entity);

// {id, name}: enough to name an entity back in a later request.
[[nodiscard]] JsonValue EntityNameJson(const EditorDocument& document, EntityId entity);
[[nodiscard]] DataFieldSchema EntityNameSchema(std::string key, std::string summary);

// {id, name, kind, position, min, max}: what every listing answers with.
[[nodiscard]] JsonValue EntityRecordJson(const EditorDocument& document, EntityId entity);
[[nodiscard]] DataFieldSchema EntityRecordSchema(std::string key = {});

// [{id, name, element, index?}]: selected entities, faces, edges or vertices.
[[nodiscard]] JsonValue SelectionJson(const EditorDocument& document, std::span<const SelectableRef> selected);
[[nodiscard]] DataFieldSchema SelectionSchema(std::string key, std::string summary);
