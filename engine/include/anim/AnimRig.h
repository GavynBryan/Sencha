#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRigData.h>
#include <assets/data/DataAssetHandle.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <world/ComponentAssetOwnership.h>

#include <tuple>

struct SENCHA_COMPONENT("sencha.anim_rig")
       SENCHA_SCHEMA("anim_rig")
       SENCHA_SCENE_CHUNK("ANRG")
AnimRig
{
    SENCHA_FIELD("rig")
    SENCHA_DATA_ASSET(kAnimRigType)
    SENCHA_LABEL("Rig")
    SENCHA_TOOLTIP("What this entity reads, stores and composes to animate.")
    DataAssetHandle Rig{};
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRig.sencha.h>
#endif

// Owns one reference to its rig. Every tier takes requests and plays content, so a
// rig brings its request set, content state and flow state.
template <>
struct ComponentTraits<AnimRig> : SchemaAssetOwnership<AnimRig>
{
    using DerivedComponents = std::tuple<AnimRequestSet, AnimContentState, AnimFlowState>;
};
