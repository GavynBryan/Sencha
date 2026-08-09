#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/RuntimeSchema.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// ReplicationSchemas
//
// Types that travel at a different precision than they are kept at.
//
// A transform is stored as floats because that is what the simulation and the
// renderer want from it. It is sent at the precision a player can tell apart,
// which is a fact about the link and the eye rather than about the type -- so
// it is stated here, by the layer doing the sending, and not on the type's own
// schema. The scene format, the inspector, the physics step and the editor all
// keep reading the same members at full width; nothing below reaches them.
//
// The members named here must be the members the type's own schema names.
// Offsets come from member pointers against the same root either way, so a
// description that named something else would be addressing bytes that are not
// where it says.
//=============================================================================

// Half-width of the position range, in metres, and the bits each axis gets.
//
// A position outside this clamps to the edge rather than wrapping, which is the
// honest failure but is still an entity in the wrong place: a world larger than
// this across cannot be replicated as authored, and would need its coordinates
// rebased per zone before it could be.
inline constexpr float kNetPositionRange = 1024.0f;

// Twenty-three bits over that range resolves to about a quarter of a millimetre
// per axis. Two more than the twenty-one that would resolve to a millimetre,
// and the reason is measured: the prediction convergence suite holds a client
// within a millimetre of the authority under packet loss, and rounding is spent
// from that same budget. At twenty-one bits the suite still passes, at a
// disagreement of 4.7e-4 to 6.8e-4 metres -- two thirds of the allowance gone
// on rounding rather than on the divergence the allowance exists to bound. At
// twenty-three it is 8.6e-5 to 1.6e-4. The two extra bits per axis cost six
// bits on a moving entity, which is under five percent of what one costs to
// describe.
inline constexpr std::uint8_t kNetPositionBits = 23;

// A unit quaternion's components are in [-1, 1] by definition, so the range is
// the type's rather than a guess. Twelve bits resolves to about two hundredths
// of a degree.
//
// Each component is quantized on its own, so a decoded rotation is very
// slightly off unit -- around a twentieth of a percent at this width, which
// reads as an imperceptible scale error through a rotation matrix. Encoding the
// smallest three components and reconstructing the fourth would be both smaller
// and exactly unit, but it needs a codec that knows what a quaternion is rather
// than one that packs scalar runs.
inline constexpr float kNetRotationRange = 1.0f;
inline constexpr std::uint8_t kNetRotationBits = 12;

// Scale is nearly always exactly one and rarely moves, so what matters is that
// the value it sits at survives the trip: a coarse range would make every
// replicated entity a few percent the wrong size, permanently and everywhere.
// Sixteen bits over this range resolves to about half a thousandth.
inline constexpr float kNetScaleRange = 16.0f;
inline constexpr std::uint8_t kNetScaleBits = 16;

template <typename T>
struct ReplicationSchema<Transform3d<T>>
{
    static constexpr std::string_view Name = "Transform3d";

    static auto Fields()
    {
        return std::tuple{
            MakeField("position", &Transform3d<T>::Position)
                .Quantize(-kNetPositionRange, kNetPositionRange, kNetPositionBits),
            MakeField("rotation", &Transform3d<T>::Rotation)
                .Quantize(-kNetRotationRange, kNetRotationRange, kNetRotationBits),
            MakeField("scale", &Transform3d<T>::Scale)
                .Quantize(-kNetScaleRange, kNetScaleRange, kNetScaleBits),
        };
    }
};
