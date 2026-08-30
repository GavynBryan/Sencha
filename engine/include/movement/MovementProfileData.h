#pragma once

#include <assets/data/DataAssetHandle.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class MovementSupportCondition : uint8_t
{
    Any,
    None,
    Stable,
    Steep,
};

struct MovementTuningPatch
{
    std::optional<float> MaxSpeed;
    std::optional<float> Acceleration;
    std::optional<float> Friction;
    std::optional<float> StopSpeed;
    std::optional<float> WishSpeedCap;
    std::optional<float> Drag;
    std::optional<float> GravityScale;
    std::optional<float> JumpSpeed;
};

struct MovementLayerCondition
{
    std::optional<std::string> Mode;
    MovementSupportCondition Support = MovementSupportCondition::Any;
    std::optional<float> ImmersionAtLeast;
    // Whether this tick's movement request asks for a jump. A fact about the
    // request rather than about the body, so it is available while tuning is
    // being resolved -- which is the tick a launch needs different coefficients
    // on, before the body has left the ground and support still reads stable.
    std::optional<bool> Jump;
    std::vector<std::string> AllTags;
    std::vector<std::string> AnyTags;
    std::vector<std::string> NoneTags;
};

struct MovementProfileLayer
{
    // Authored label. Empty means unnamed; authoring and diagnostic surfaces
    // synthesize a label from the condition instead. Never compared or matched
    // against, so it carries no runtime identity.
    std::string Name;
    MovementLayerCondition When;
    MovementTuningPatch Set;
    MovementTuningPatch Scale;
    MovementTuningPatch Add;
    std::string SourcePath;
};

struct MovementModeProfile
{
    std::string Mode;
    std::vector<std::string> SustainAllTags;
    std::vector<std::string> SustainAnyTags;
    std::vector<std::string> SustainNoneTags;
    std::vector<MovementProfileLayer> Layers;
};

struct CompiledMovementProfile
{
    std::string Name;
    std::vector<MovementProfileLayer> Layers;
    std::vector<MovementModeProfile> Modes;
};

// The subtype a movement profile declares in its .sdata envelope. Named here
// so a component field can say which data assets it accepts and a picker can
// offer only those.
inline constexpr std::string_view kMovementProfileTypeName = "movement.profile";

struct MovementProfileHandle
{
    DataAssetHandle Value;

    [[nodiscard]] bool IsValid() const { return Value.IsValid(); }
    auto operator<=>(const MovementProfileHandle&) const = default;
};

void RegisterMovementProfileData(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas);
void UnregisterMovementProfileData(DataAssetTypeRegistry& types,
                                   DataSchemaRegistry& schemas);
