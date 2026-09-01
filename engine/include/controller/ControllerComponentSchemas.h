#pragma once

#include <controller/LookOrientation.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/MathSchemas.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and replication policy for the controller components.
//
// Registration and the serializers include this; the systems that read these
// components do not.
//=============================================================================

// Saved and sent both. It is sent because where something aims is one of the
// few facts other machines must see; it is saved because the pitch limits are
// a property of the thing and because an authored yaw is how a placed body
// states which way it begins facing -- the only way to state it for a body
// carrying AimFacing, whose rotation is overwritten from the first tick.
template <>
struct TypeSchema<LookOrientation>
{
    static constexpr std::string_view Name = "LookOrientation";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('L', 'O', 'O', 'K');
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        const LookOrientation defaults;
        return std::tuple{
            // Yaw accumulates without bound -- it is a running total, not an
            // angle folded into a circle -- so a fixed quantization range would
            // clamp a player who kept turning one way. It ships at full width
            // until the codec can carry a wrapping angle.
            MakeField("yaw", &LookOrientation::Yaw)
                .Default(defaults.Yaw)
                .OwnerLocal()
                .Degrees()
                .Label("Facing")
                .Tooltip("Which way this body starts out aiming. Stored in "
                         "radians; a body carrying AimFacing begins turned to "
                         "this rather than to its authored rotation."),
            // Pitch is bounded by the limits below, which are stricter than
            // this range, so nothing here can clamp.
            MakeField("pitch", &LookOrientation::Pitch)
                .Default(defaults.Pitch)
                .Quantize(-1.5707964f, 1.5707964f, 16)
                .OwnerLocal()
                .Degrees()
                .Label("Elevation")
                .Tooltip("How far up or down this body starts out aiming."),
            // How far this thing can look is a property of the thing, identical
            // on every machine that loaded it. Sending it every tick would be
            // sending a constant.
            MakeField("min_pitch", &LookOrientation::MinPitch)
                .Default(defaults.MinPitch).LocalOnly()
                .Degrees()
                .Label("Look down limit")
                .Tooltip("How far down this thing can aim. A property of the "
                         "thing: a neck and a tank turret differ."),
            MakeField("max_pitch", &LookOrientation::MaxPitch)
                .Default(defaults.MaxPitch).LocalOnly()
                .Degrees()
                .Label("Look up limit")
                .Tooltip("How far up this thing can aim."),
        };
    }
};

// The tag's presence is its whole value, so the schema has no fields; it is
// here so content can opt a placed body in without any code naming it.
template <>
struct TypeSchema<AimFacing>
{
    static constexpr std::string_view Name = "AimFacing";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'I', 'M', 'F');

    static auto Fields() { return std::tuple{}; }
};
