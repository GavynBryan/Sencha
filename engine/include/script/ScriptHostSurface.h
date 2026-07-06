#pragma once

#include <script/ScriptModule.h>

#include <cstdint>
#include <span>
#include <string_view>

//=============================================================================
// ScriptHostSurface
//
// The engine's native (host) component surface as scripts see it: which native
// components a script may name and their fields. This is the single source both
// the compiler (type-checking and bind emission) and the linker (offset
// resolution) derive from, so the two cannot disagree on the field set, scalar
// kinds, or slot counts. Byte offsets live here too and are checked against the
// real struct layout in ScriptLink.
//
// Fields are listed at the granularity the author uses (a whole vec3, not its
// axes); the linker derives the per-axis leaves by adding the scalar stride.
//=============================================================================

struct ScriptHostField
{
    std::string_view ScriptName; // "position"
    std::string_view BindPath;   // "local.position"
    ScriptScalarKind Scalar;
    std::uint8_t SlotCount;
    std::uint16_t ByteOffset; // into the native component's storage
};

struct ScriptHostComponent
{
    std::string_view Name; // script-visible and bind name: "Transform"
    std::span<const ScriptHostField> Fields;
};

[[nodiscard]] std::span<const ScriptHostComponent> ScriptHostComponents();
