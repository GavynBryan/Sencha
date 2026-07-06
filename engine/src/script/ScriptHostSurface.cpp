#include <script/ScriptHostSurface.h>

std::span<const ScriptHostComponent> ScriptHostComponents()
{
    // LocalTransform stores a Transform3f: f32 position at byte 0, rotation at
    // 12, f32 scale at 28. Position and scale are the script-writable fields;
    // rotation is not exposed in v1. The offsets are verified against the
    // struct layout by static_assert in ScriptLink.
    static constexpr ScriptHostField kTransformFields[] = {
        { "position", "local.position", ScriptScalarKind::Float, 3, 0 },
        { "scale", "local.scale", ScriptScalarKind::Float, 3, 28 },
    };
    static constexpr ScriptHostComponent kComponents[] = {
        { "Transform", kTransformFields },
    };
    return kComponents;
}
