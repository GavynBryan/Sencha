#pragma once

#include <ecs/EntityId.h>

class World;

// What the document says about an entity's part in the lighting, as reads over
// its components.
namespace LightingReadModel
{
// A light whose contribution is baked rather than evaluated at runtime, of any
// light type that can carry that setting.
[[nodiscard]] bool BakesDirect(const World& components, EntityId entity);

[[nodiscard]] bool IsIrradianceVolume(const World& components, EntityId entity);
}
