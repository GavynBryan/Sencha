#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The scene form of an entity's responses: its bindings verbatim as authored,
// so a World that cannot resolve one of their names still saves them. Runtime
// state is not scene data; a zone remembers it (logic/ResponsesSystem.h).
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeResponsesSerializer();
