#pragma once

#include "EditorComponentAdapter.h"

#include <ecs/EntityId.h>

#include <memory>

class World;

[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeHeldEditorAdapter();

[[nodiscard]] bool IsHeldWithin(const World& world, EntityId entity, EntityId holder);
