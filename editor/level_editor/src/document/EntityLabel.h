#pragma once

#include <ecs/EntityId.h>

#include <string>

class EditorDocument;

// What an entity is called where a person reads it: its authored name, the
// source a nameless placement came from, or its handle.
[[nodiscard]] std::string EntityLabel(const EditorDocument& document, EntityId entity);
