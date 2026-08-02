#pragma once

#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementComponents.h>
#include <movement/MovementProfileData.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class GameplayTagRegistry;

struct BoundMovementTagQuery
{
    std::vector<GameplayTagId> All;
    std::vector<GameplayTagId> Any;
    std::vector<GameplayTagId> None;
};

struct BoundMovementLayerCondition
{
    std::optional<LocomotionModeId> Mode;
    MovementSupportCondition Support = MovementSupportCondition::Any;
    std::optional<float> ImmersionAtLeast;
    BoundMovementTagQuery Tags;
};

struct BoundMovementLayer
{
    BoundMovementLayerCondition When;
    MovementTuningPatch Set;
    MovementTuningPatch Scale;
    MovementTuningPatch Add;
    std::string SourcePath;
};

struct BoundMovementModeProfile
{
    LocomotionModeId Mode;
    BoundMovementTagQuery Sustain;
    std::vector<BoundMovementLayer> Layers;
};

struct BoundMovementProfile
{
    std::vector<BoundMovementLayer> Layers;
    std::vector<BoundMovementModeProfile> Modes;
};

struct MovementProfileBindResult
{
    std::optional<BoundMovementProfile> Profile;
    std::string Error;

    [[nodiscard]] bool IsValid() const { return Profile.has_value() && Error.empty(); }
};

using ResolveLocomotionModeFn =
    std::function<LocomotionModeId(std::string_view name)>;

[[nodiscard]] MovementProfileBindResult BindMovementProfile(
    const CompiledMovementProfile& profile,
    const GameplayTagRegistry& tags,
    const ResolveLocomotionModeFn& resolveMode);

struct MovementResolveContext
{
    SupportKind Support = SupportKind::None;
    float Immersion = 0.0f;
    LocomotionModeId Mode;
    const GameplayTagContainer* Tags = nullptr;
};

struct MovementLayerTrace
{
    std::string SourcePath;
    bool Matched = false;
    std::string Failure;
};

struct MovementResolveResult
{
    ResolvedMovementTuning Tuning;
    std::vector<MovementLayerTrace> Trace;
};

[[nodiscard]] MovementResolveResult ResolveMovementTuning(
    const BoundMovementProfile& profile,
    const MovementResolveContext& context,
    float attributeMaxSpeed,
    bool collectTrace = false);

[[nodiscard]] bool MovementModeSustainMatches(
    const BoundMovementProfile& profile,
    LocomotionModeId mode,
    const GameplayTagContainer& tags);
