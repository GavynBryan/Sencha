#pragma once

#include "EditorComponentAdapter.h"

#include <ecs/EntityId.h>

#include <memory>

class World;

// docs/gameplay/held-records.md, "Authoring".
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeHeldEditorAdapter();

// Whether `entity` is `holder` or is held beneath it, by the document's Held components.
[[nodiscard]] bool IsHeldWithin(const World& world, EntityId entity, EntityId holder);
