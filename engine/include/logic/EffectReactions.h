#pragma once

#include <assets/data/DataAssetHandle.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <logic/EffectReactionProfile.h>
#include <world/ComponentAssetOwnership.h>

// Answers the effects applied to this entity by its profile: a push, a shake of
// the view it owns. docs/gameplay/reactions.md.
struct SENCHA_COMPONENT("sencha.effect_reactions")
       SENCHA_SCHEMA("EffectReactions")
       SENCHA_SCENE_CHUNK("EFRX")
EffectReactions
{
    SENCHA_FIELD("profile")
    SENCHA_DATA_ASSET(kEffectReactionProfileType)
    SENCHA_LABEL("Profile")
    SENCHA_TOOLTIP("How this entity answers the effects applied to it.")
    DataAssetHandle Profile{};
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/EffectReactions.sencha.h>
#endif

// Owns one reference to its profile, so the profile stays loaded with the entity.
template <>
struct ComponentTraits<EffectReactions> : SchemaAssetOwnership<EffectReactions>
{
};
