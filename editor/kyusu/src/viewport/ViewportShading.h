#pragma once

#include <cstddef>
#include <cstdint>

// How a viewport draws brush bodies. A per-viewport preference (defaulted by
// orientation, overridable at runtime), realized by the render feature picking a
// matching body-render strategy — so neither the renderers nor the dispatch hard-
// code which view is which. Adding a mode is a new enum value + a new strategy.
enum class ViewportShading : std::uint8_t
{
    Wireframe, // ortho grid views: precise, see-through
    Solid,     // perspective view: lit, UV-checkered preview

    Count
};

inline constexpr std::size_t ViewportShadingCount = static_cast<std::size_t>(ViewportShading::Count);
