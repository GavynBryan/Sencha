#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// How a view looks, resolved from the layers that author it: the game, a world,
// a graph and a scene. Docs: docs/renderer/presentation-settings.md.
inline constexpr std::string_view kPresentationSettingsType = "presentation.settings";
inline constexpr std::string_view kGamePresentationAsset = "asset://data/presentation.sdata";
inline constexpr std::uint32_t kPresentationSettingsVersion = 1;

struct BloomSettings
{
    bool Enabled = false;
    float Threshold = 1.0f;
    float Intensity = 1.0f;
    float Spread = 1.0f;

    friend bool operator==(const BloomSettings&, const BloomSettings&) = default;
};

struct PresentationSettings
{
    BloomSettings Bloom;

    friend bool operator==(const PresentationSettings&, const PresentationSettings&) = default;
};

struct BloomSettingsLayer
{
    std::optional<bool> Enabled;
    std::optional<float> Threshold;
    std::optional<float> Intensity;
    std::optional<float> Spread;

    friend bool operator==(const BloomSettingsLayer&, const BloomSettingsLayer&) = default;
};

// One layer's say: every field it leaves out is inherited from the layer below.
struct PresentationSettingsLayer
{
    BloomSettingsLayer Bloom;

    [[nodiscard]] bool IsEmpty() const { return *this == PresentationSettingsLayer{}; }
    friend bool operator==(const PresentationSettingsLayer&, const PresentationSettingsLayer&) = default;
};

[[nodiscard]] const DataSchema& PresentationSettingsSchema();

// False with `error` naming the field when `value` does not satisfy the schema.
[[nodiscard]] bool ReadPresentationSettingsLayer(const JsonValue& value, PresentationSettingsLayer& out,
                                                 std::string& error);
// Only the fields the layer sets; an empty layer writes an empty object.
[[nodiscard]] JsonValue WritePresentationSettingsLayer(const PresentationSettingsLayer& layer);

void ApplyPresentationSettingsLayer(PresentationSettings& settings, const PresentationSettingsLayer& layer);
// A layer that sets every field to the value `settings` resolved to.
[[nodiscard]] PresentationSettingsLayer AsPresentationLayer(const PresentationSettings& settings);

void RegisterPresentationSettingsData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
