#pragma once

#include <cstdint>
#include <string_view>

// Diagnostic channels rendered by the development-only forward pipeline.
// The numeric values are the frame-UBO contract used by
// mesh_debug_view.frag.glsl.
enum class RenderDebugView : std::uint32_t
{
    None = 0,
    WorldNormals,
    NormalMap,
    NormalDelta,
    Diffuse,
    Specular,
    Emission,
    Roughness,
    LightComplexity,
    ShadowFiltered,
    ShadowRaw,
    Overdraw,
    Count,
};

inline constexpr std::uint32_t kRenderDebugViewCount =
    static_cast<std::uint32_t>(RenderDebugView::Count);

[[nodiscard]] const char* ToString(RenderDebugView view);
[[nodiscard]] const char* RenderDebugViewLabel(RenderDebugView view);

// Case-sensitive cvar form. Unknown names leave `view` untouched.
[[nodiscard]] bool ParseRenderDebugView(std::string_view name,
                                        RenderDebugView& view);
