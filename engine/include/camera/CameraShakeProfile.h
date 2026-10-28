#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <camera/CameraEffectTiming.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <math/Vec.h>

#include <optional>
#include <string>
#include <string_view>

inline constexpr std::string_view kCameraShakeType = "camera.shake";

// Camera-local amounts. Rotation is pitch, yaw, roll in degrees (positive pitch
// looks up); translation is metres along the camera's right, up and back axes.
struct CameraShakeChannels
{
    Vec3d Rotation = Vec3d::Zero();
    Vec3d Translation = Vec3d::Zero();

    friend bool operator==(const CameraShakeChannels&, const CameraShakeChannels&) = default;
};

// Distance from the shake's point at which it starts to weaken, and where it is gone.
struct CameraShakeFalloff
{
    float Inner = 0.0f;
    float Outer = 0.0f;

    friend bool operator==(const CameraShakeFalloff&, const CameraShakeFalloff&) = default;
};

// Noise oscillating at Frequency plus a kick held in one direction, both scaled
// by the envelope. See docs/gameplay/camera.md, "Camera shake".
struct CameraShakeProfile
{
    CameraShakeChannels Noise;
    CameraShakeChannels Kick;
    float Frequency = 0.0f;
    CameraEnvelope Envelope;
    std::optional<CameraShakeFalloff> Falloff;

    friend bool operator==(const CameraShakeProfile&, const CameraShakeProfile&) = default;
};

// Reads a profile, needing no World. False with `error` naming the field when
// a value is not finite, a time is negative, the frequency cannot carry the
// noise, or the falloff is not a range.
[[nodiscard]] bool CompileCameraShakeProfile(const JsonValue& data, CameraShakeProfile& out, std::string& error);

void RegisterCameraShakeData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
