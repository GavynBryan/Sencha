#include <camera/CameraShakeProfile.h>

#include <cmath>
#include <format>
#include <memory>

namespace
{
// Beyond this a shake aliases at any frame rate a display presents.
constexpr double kMaxFrequency = 120.0;

bool Number(const JsonValue& object, std::string_view key, float& out, std::string_view path, std::string& error)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
        return true;
    if (!value->IsNumber() || !std::isfinite(value->AsNumber()))
    {
        error = std::format("{}.{}: a finite number", path, key);
        return false;
    }
    out = static_cast<float>(value->AsNumber());
    return true;
}

bool Vector(const JsonValue& object, std::string_view key, Vec3d& out, std::string_view path, std::string& error)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
        return true;
    if (!value->IsArray() || value->AsArray().size() != 3)
    {
        error = std::format("{}.{}: three numbers", path, key);
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const JsonValue& component = value->AsArray()[axis];
        if (!component.IsNumber() || !std::isfinite(component.AsNumber()))
        {
            error = std::format("{}.{}[{}]: a finite number", path, key, axis);
            return false;
        }
        out[axis] = static_cast<float>(component.AsNumber());
    }
    return true;
}

bool Channels(const JsonValue& data, std::string_view key, CameraShakeChannels& out, std::string& error)
{
    const JsonValue* channels = data.Find(key);
    if (channels == nullptr)
        return true;
    const std::string path = std::format("$.data.{}", key);
    if (!channels->IsObject())
    {
        error = path + ": an object";
        return false;
    }
    return Vector(*channels, "rotation", out.Rotation, path, error)
        && Vector(*channels, "translation", out.Translation, path, error);
}

bool Moves(const CameraShakeChannels& channels)
{
    return channels.Rotation != Vec3d::Zero() || channels.Translation != Vec3d::Zero();
}

DataFieldSchema Optional(DataFieldSchema field)
{
    field.Required = false;
    return field;
}

DataFieldSchema Seconds(std::string key, std::string label, std::string summary)
{
    DataFieldSchema field = Optional(MakeDataField(DataFieldKind::Float, std::move(key), std::move(label),
                                                   std::move(summary)));
    field.Units = "s";
    field.Default = 0.0;
    field.Numeric.Minimum = 0.0;
    return field;
}

DataFieldSchema ChannelsField(std::string key, std::string label, std::string summary)
{
    DataFieldSchema rotation = Optional(MakeDataField(DataFieldKind::Vector, "rotation", "Rotation",
                                                      "Pitch, yaw and roll; positive pitch looks up."));
    rotation.Units = "deg";
    DataFieldSchema translation = Optional(MakeDataField(DataFieldKind::Vector, "translation", "Translation",
                                                         "Along the camera's right, up and back."));
    translation.Units = "m";
    DataFieldSchema channels = Optional(MakeDataField(DataFieldKind::Record, std::move(key), std::move(label),
                                                      std::move(summary)));
    channels.Children = { std::move(rotation), std::move(translation) };
    return channels;
}

DataSchema MakeSchema()
{
    DataFieldSchema frequency = Optional(MakeDataField(DataFieldKind::Float, "frequency", "Frequency",
                                                       "How fast the noise oscillates."));
    frequency.Units = "Hz";
    frequency.Default = 0.0;
    frequency.Numeric.Minimum = 0.0;
    frequency.Numeric.Maximum = kMaxFrequency;
    DataFieldSchema inner = MakeDataField(DataFieldKind::Float, "inner", "Inner", "Full strength within this.");
    inner.Units = "m";
    inner.Numeric.Minimum = 0.0;
    DataFieldSchema outer = MakeDataField(DataFieldKind::Float, "outer", "Outer", "Gone beyond this.");
    outer.Units = "m";
    outer.Numeric.Minimum = 0.0;
    DataFieldSchema falloff = Optional(MakeDataField(DataFieldKind::Record, "falloff", "Falloff",
                                                     "How it weakens away from the point it is given."));
    falloff.Children = { std::move(inner), std::move(outer) };

    DataSchema schema;
    schema.TypeName = std::string(kCameraShakeType);
    schema.DisplayName = "Camera shake";
    schema.Description = "Moves a view from its camera for a moment, without moving the camera.";
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children = {
        ChannelsField("noise", "Noise", "The most each axis oscillates."),
        ChannelsField("kick", "Kick", "A fixed displacement, eased in and out by the envelope."),
        std::move(frequency),
        Seconds("attack", "Attack", "Rising to full strength."),
        Seconds("hold", "Hold", "At full strength, when not held by a lease."),
        Seconds("release", "Release", "Falling back to nothing."),
        std::move(falloff),
    };
    return schema;
}

DataAssetCompileResult Compile(const JsonValue& data)
{
    auto profile = std::make_shared<CameraShakeProfile>();
    std::string error;
    if (!CompileCameraShakeProfile(data, *profile, error))
        return { {}, {}, std::move(error) };
    return { std::move(profile), {}, {} };
}
} // namespace

bool CompileCameraShakeProfile(const JsonValue& data, CameraShakeProfile& out, std::string& error)
{
    out = {};
    if (!data.IsObject())
    {
        error = "$.data: an object";
        return false;
    }
    if (!Channels(data, "noise", out.Noise, error) || !Channels(data, "kick", out.Kick, error)
        || !Number(data, "frequency", out.Frequency, "$.data", error)
        || !Number(data, "attack", out.Envelope.Attack, "$.data", error)
        || !Number(data, "hold", out.Envelope.Hold, "$.data", error)
        || !Number(data, "release", out.Envelope.Release, "$.data", error))
        return false;
    if (!IsValidCameraEnvelope(out.Envelope))
    {
        error = "$.data: attack, hold and release are not negative";
        return false;
    }
    if (out.Frequency < 0.0f || out.Frequency > kMaxFrequency)
    {
        error = std::format("$.data.frequency: between 0 and {} Hz", kMaxFrequency);
        return false;
    }
    if (Moves(out.Noise) && out.Frequency <= 0.0f)
    {
        error = "$.data.frequency: noise needs a frequency above zero";
        return false;
    }
    if (const JsonValue* falloff = data.Find("falloff"))
    {
        CameraShakeFalloff range;
        if (!falloff->IsObject() || falloff->Find("inner") == nullptr || falloff->Find("outer") == nullptr)
        {
            error = "$.data.falloff: an object with inner and outer";
            return false;
        }
        if (!Number(*falloff, "inner", range.Inner, "$.data.falloff", error)
            || !Number(*falloff, "outer", range.Outer, "$.data.falloff", error))
            return false;
        if (range.Inner < 0.0f || range.Outer <= range.Inner)
        {
            error = "$.data.falloff: 0 <= inner < outer";
            return false;
        }
        out.Falloff = range;
    }
    return true;
}

void RegisterCameraShakeData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kCameraShakeType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kCameraShakeType);
}
