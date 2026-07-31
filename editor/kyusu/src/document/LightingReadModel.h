#pragma once

#include <cstdint>

class World;

// Counts the lighting authoring surfaces a panel reports on. These are reads
// over the document, named so they sit beside the document rather than inline in
// the UI construction that happens to display them.
namespace LightingReadModel
{
// Lights whose contribution is baked rather than evaluated at runtime, across
// every light type that can carry that setting.
[[nodiscard]] std::uint32_t CountDirectBakeLights(const World& components);

// Irradiance volumes placed by the author, whatever the cook has made of them.
[[nodiscard]] std::uint32_t CountAuthoredIrradianceVolumes(const World& components);
}
