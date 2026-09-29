#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimEventCursor.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimRequestReport.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRigComposition.h>
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

// Owns one reference to its rig. Every rig takes requests, plays content and crosses
// its marks, so it brings what each of those owns; anything else is its binding's to
// say (AnimRigCompositionSystem).
template <>
struct ComponentTraits<AnimRig> : SchemaAssetOwnership<AnimRig>
{
    using DerivedComponents =
        std::tuple<AnimRequestSet, AnimContentState, AnimEventCursor, AnimRequestReport, AnimRigComposition>;
};
