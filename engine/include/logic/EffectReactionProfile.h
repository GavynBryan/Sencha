#pragma once

#include <attributes/AttributeId.h>
#include <effects/EffectSource.h>
#include <gameplay_tags/GameplayTagId.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AttributeRegistry;
class DataAssetTypeRegistry;
class DataSchemaRegistry;
struct GameplayTagContainer;
class GameplayTagRegistry;
class GameplayTagSet;
struct EffectAttributeChange;

// How an entity answers the effects applied to it, as authored content: each
// reaction matches an effect's kinds and the way one attribute moved, and pushes
// the entity and shakes its view in proportion to how far it moved, and asks its
// rig to flinch from the side the blow came from.
// docs/gameplay/reactions.md.
inline constexpr std::string_view kEffectReactionProfileType = "effect.reaction_profile";

struct EffectReactionDeclaration
{
    GameplayTagQueryNames Kinds;
    GameplayTagQueryNames Recipient;
    std::string Attribute;
    // Answers a fall in the attribute; otherwise a rise.
    bool Decrease = true;
    // The least change, in the attribute's units, that is answered at all.
    float Threshold = 0.0f;
    // Metres per second of push per unit of change, up to the cap.
    float ImpulsePerUnit = 0.0f;
    float ImpulseMax = 0.0f;
    // Share of the push added straight up.
    float Lift = 0.0f;
    // A camera.shake profile path; empty shakes nothing.
    std::string ShakeProfile;
    float ShakePerUnit = 0.0f;
    float ShakeMax = 0.0f;
    // Animation intents by the side the blow came from; empty plays nothing. Front
    // answers a blow with no direction, and any side that names none of its own.
    std::string AnimationFront;
    std::string AnimationBack;
    std::string AnimationLeft;
    std::string AnimationRight;
    std::uint32_t AnimationTicks = 0;
};

struct EffectReactionProfile
{
    std::vector<EffectReactionDeclaration> Reactions;
};

void RegisterEffectReactionProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// One reaction resolved against a World's registries.
struct EffectReactionRule
{
    GameplayTagQuery Kinds;
    GameplayTagQuery Recipient;
    AttributeId Attribute;
    bool Decrease = true;
    float Threshold = 0.0f;
    float ImpulsePerUnit = 0.0f;
    float ImpulseMax = 0.0f;
    float Lift = 0.0f;
    std::string ShakeProfile;
    float ShakePerUnit = 0.0f;
    float ShakeMax = 0.0f;
    GameplayTagId AnimationFront;
    GameplayTagId AnimationBack;
    GameplayTagId AnimationLeft;
    GameplayTagId AnimationRight;
    std::uint32_t AnimationTicks = 0;
};

// The rules `profile` means in a World with these registries, or false with one
// message per name neither registry knows.
[[nodiscard]] bool ResolveEffectReactionProfile(const EffectReactionProfile& profile,
                                                const AttributeRegistry& attributes,
                                                const GameplayTagRegistry& tags,
                                                std::vector<EffectReactionRule>& out,
                                                std::vector<std::string>& errors);

// One application as a reaction sees it.
struct EffectReactionCase
{
    const GameplayTagSet* Kinds = nullptr;
    // The recipient's tags; none matches only a rule that asks nothing of them.
    const GameplayTagContainer* Recipient = nullptr;
    std::span<const EffectAttributeChange> Changes;
    EffectSource Source;
    Vec3d TargetPosition = Vec3d::Zero();
    // Where the instigator stood, when it still exists.
    std::optional<Vec3d> InstigatorPosition;
    Vec3d Up = Vec3d(0.0f, 1.0f, 0.0f);
    // Which way the recipient faces, for the side a blow came from.
    std::optional<Vec3d> Forward = std::nullopt;
};

struct EffectReactionOutcome
{
    std::size_t Rule = 0;
    // How far the attribute moved the way the rule answers.
    float Units = 0.0f;
    // Zero when the rule pushes nothing or the case has no direction to push along.
    Vec3d Impulse = Vec3d::Zero();
    float ShakeScale = 0.0f;
    // What to ask the recipient's rig to play, and for how long; invalid when the rule asks nothing.
    GameplayTagId AnimationIntent;
    std::uint32_t AnimationTicks = 0;
};

// The first rule that answers the case, and what it does; nullopt when none does.
// The push runs along the strike, else away from its point, else away from the
// instigator, flattened onto the ground plane `Up` defines.
[[nodiscard]] std::optional<EffectReactionOutcome> ReactToEffect(std::span<const EffectReactionRule> rules,
                                                                 const EffectReactionCase& application,
                                                                 const GameplayTagRegistry& tags);
