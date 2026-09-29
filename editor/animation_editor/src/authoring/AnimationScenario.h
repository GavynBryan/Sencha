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

inline constexpr std::string_view kAnimationScenarioType = "animation.preview_scenario";
inline constexpr int kAnimationScenarioVersion = 1;

// Interpreted by the kind of slot or parameter it is applied to once the rig binds.
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
    // Tag name.
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

    // IssueRequest, CancelRequest. A cancel targets the participant's live request for the intent.
    std::string Participant;
    std::string Intent;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 0;
    std::uint8_t Layers = kAnimAllLayers;
    std::vector<std::pair<std::string, AnimationScenarioValue>> Params;
    AnimCancelReason Reason = AnimCancelReason::Released;

    JsonValue::Object Unknown;
};

// Only an authority produces gameplay events.
enum class AnimationPreviewRole : std::uint8_t
{
    Authority,
    Client,
};

// Metres, in the preview's world space.
struct AnimationScenarioWall
{
    Vec3d Center = Vec3d::Zero();
    Vec3d HalfExtents = Vec3d(0.5f, 1.0f, 0.5f);

    friend bool operator==(const AnimationScenarioWall&, const AnimationScenarioWall&) = default;
};

// The character stands on a floor at y = 0 and moves through the game's
// movement pipeline against these walls.
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
    // Registered into the preview World only, standing in for a game module's names.
    std::vector<std::string> DeclaredTags;
    std::vector<std::pair<std::string, AnimationScenarioValue>> Inputs;
    // Ordered by tick; actions on one tick apply in list order.
    std::vector<AnimationScenarioAction> Actions;
    AnimationPreviewRole Role = AnimationPreviewRole::Authority;
    // Verb names; any other verb answers Unavailable in the preview.
    std::vector<std::string> Recorders;
    // Empty: the character does not move.
    std::optional<AnimationScenarioMovement> Movement;

    JsonValue::Object Unknown;

    // Inserts after every action already on its tick.
    void Append(AnimationScenarioAction action);
    void TruncateAfter(AnimTick tick);
    [[nodiscard]] bool HasParticipant(std::string_view name) const;
};

[[nodiscard]] JsonValue WriteAnimationScenario(const AnimationScenario& scenario);

// Null only when the document is not a scenario; field problems go to `diagnostics`.
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
