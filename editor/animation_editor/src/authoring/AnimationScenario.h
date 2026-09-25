#pragma once

#include <anim/AnimDiagnostic.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//=============================================================================
// AnimationScenario
//
// An editor-only sidecar describing a reproducible preview run: the rig, the
// fixed tick rate, the named participants that issue requests, the initial fact
// inputs, and a tick-ordered list of actions. Names stay names -- facts, intents,
// parameters and tags resolve against the rig and vocabulary when the scenario
// runs -- so a scenario runs against any rig that declares what it uses, and a
// name that does not resolve is a diagnostic rather than a silent default.
//
// Unknown top-level and per-action fields are kept and written back unchanged.
//=============================================================================

inline constexpr std::string_view kAnimationScenarioType = "animation.preview_scenario";
inline constexpr int kAnimationScenarioVersion = 1;

// A value as a scenario states it. Which of these is read depends on the kind
// of the slot or parameter it is applied to, known only once the rig binds.
struct AnimationScenarioValue
{
    enum class Kind : std::uint8_t
    {
        Bool,
        Number,
        Name,
    };

    Kind Type = Kind::Number;
    bool Bool = false;
    double Number = 0.0;
    // A tag, by name.
    std::string Name;

    static AnimationScenarioValue FromBool(bool value);
    static AnimationScenarioValue FromNumber(double value);
    static AnimationScenarioValue FromName(std::string value);

    friend bool operator==(const AnimationScenarioValue&, const AnimationScenarioValue&) = default;
};

enum class AnimationScenarioActionKind : std::uint8_t
{
    SetFact,
    ClearFact,
    IssueRequest,
    CancelRequest,
};

struct AnimationScenarioAction
{
    AnimTick Tick = 0;
    AnimationScenarioActionKind Kind = AnimationScenarioActionKind::SetFact;

    // SetFact, ClearFact.
    std::string Fact;
    AnimationScenarioValue Value;

    // IssueRequest, CancelRequest. The participant is the request's source;
    // the cancel names the source's live request for the intent.
    std::string Participant;
    std::string Intent;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 0;
    std::uint8_t Layers = kAnimAllLayers;
    std::vector<std::pair<std::string, AnimationScenarioValue>> Params;
    AnimCancelReason Reason = AnimCancelReason::Released;

    JsonValue::Object Unknown;
};

// Which side of a session the preview World plays. An authority produces
// gameplay events; a client never does. Part of the scenario because it
// changes what a run does.
enum class AnimationPreviewRole : std::uint8_t
{
    Authority,
    Client,
};

// A box the previewed character can run into: its centre and half extents,
// in metres, in the preview's world.
struct AnimationScenarioWall
{
    Vec3d Center = Vec3d::Zero();
    Vec3d HalfExtents = Vec3d(0.5f, 1.0f, 0.5f);

    friend bool operator==(const AnimationScenarioWall&, const AnimationScenarioWall&) = default;
};

// The previewed character stands on a floor at y = 0 and moves: through the
// production movement pipeline and mover, with whatever root motion its
// clips carry, against these walls.
struct AnimationScenarioMovement
{
    std::vector<AnimationScenarioWall> Walls;

    friend bool operator==(const AnimationScenarioMovement&, const AnimationScenarioMovement&) = default;
};

struct AnimationScenario
{
    std::string Name;
    std::string RigPath;
    std::uint32_t TickRate = 60;
    std::uint64_t Seed = 0;
    std::vector<std::string> Participants;
    // Gameplay tags this scenario declares as preview fixtures, for names a
    // game module would declare when the editor runs without one. Registered
    // into the preview World only, and listed as fixtures wherever shown.
    std::vector<std::string> DeclaredTags;
    std::vector<std::pair<std::string, AnimationScenarioValue>> Inputs;
    // Ordered by tick; actions on one tick apply in list order.
    std::vector<AnimationScenarioAction> Actions;
    AnimationPreviewRole Role = AnimationPreviewRole::Authority;
    // Verbs a preview recorder stands behind, by name. The preview has no
    // game running, so a declared verb is otherwise Unavailable; a recorder
    // accepts and keeps what it was handed, and is always shown as one.
    std::vector<std::string> Recorders;
    // Absent: the character stays where it stands, as a pose on its own.
    std::optional<AnimationScenarioMovement> Movement;

    JsonValue::Object Unknown;

    // Inserts after every action already on its tick.
    void Append(AnimationScenarioAction action);
    // Drops every action after `tick`: acting at a tick the scenario has
    // already scripted past starts a new branch from there.
    void TruncateAfter(AnimTick tick);
    [[nodiscard]] bool HasParticipant(std::string_view name) const;
};

[[nodiscard]] JsonValue WriteAnimationScenario(const AnimationScenario& scenario);

// Null on a document that is not a scenario at all; otherwise the scenario and
// any field-located problems in `diagnostics`. `assetPath` names the sidecar
// in those diagnostics.
[[nodiscard]] std::optional<AnimationScenario> ReadAnimationScenario(
    const JsonValue& document,
    std::string_view assetPath,
    std::vector<AnimDiagnostic>& diagnostics);

[[nodiscard]] bool SaveAnimationScenario(const AnimationScenario& scenario,
                                         const std::string& filePath,
                                         std::string& error);
[[nodiscard]] std::optional<AnimationScenario> LoadAnimationScenario(
    const std::string& filePath,
    std::vector<AnimDiagnostic>& diagnostics);
