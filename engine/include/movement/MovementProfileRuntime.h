#pragma once

#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <movement/MovementProfileData.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class GameplayTagRegistry;

// Pairs each authored coefficient with the resolved member it feeds. Layer
// application, authoring surfaces, and diagnostics walk this table so the
// coefficient list exists once rather than once per consumer.
struct MovementTuningField
{
    std::string_view Key; // matches the authoring schema key
    std::optional<float> MovementTuningPatch::* Patch;
    float ResolvedMovementTuning::* Resolved;
};

[[nodiscard]] std::span<const MovementTuningField> MovementTuningFields();

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
    std::optional<bool> Jump;
    BoundMovementTagQuery Tags;
};

struct BoundMovementLayer
{
    // Carried through binding so diagnostics can label a layer without
    // reaching back into the compiled asset.
    std::string Name;
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
    bool Jump = false;
    LocomotionModeId Mode;
    const GameplayTagContainer* Tags = nullptr;
};

// A self-describing record of one layer's contribution, so a diagnostic
// surface never has to re-derive which layer an entry came from.
struct MovementLayerTrace
{
    std::string Name; // authored label; empty when the layer is unnamed
    std::string SourcePath;
    bool Matched = false;
    std::string Failure;

    // Coefficients after this layer applied, or the running values unchanged
    // when it did not match. Only meaningful when a trace was collected.
    ResolvedMovementTuning After;
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
