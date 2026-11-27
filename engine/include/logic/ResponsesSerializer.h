#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The scene form of an entity's responses: its bindings verbatim as authored,
// so a World that cannot resolve one of their names still saves them; a save's
// form adds each binding's state (docs/gameplay/responses.md).
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeResponsesSerializer();
