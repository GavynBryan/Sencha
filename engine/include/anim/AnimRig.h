#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimRigData.h>
#include <assets/data/DataAssetHandle.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <world/ComponentAssetOwnership.h>

#include <tuple>

//=============================================================================
// AnimRig
//
// Which rig an animated entity is. Every animated entity carries one; what
// else it carries -- facts, history, selector state, flows, pose -- is its
// tier. The rig asset is shared and immutable; everything World-local about it
// (slot indices, tag ids) lives in AnimRigBindings, keyed by this handle.
//=============================================================================
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

// The component owns one reference to its rig for as long as it carries it.
// Every tier takes requests, so a rig brings its request set; facts and
// history are what an entity adds to become more than a Prop.
template <>
struct ComponentTraits<AnimRig> : SchemaAssetOwnership<AnimRig>
{
    using DerivedComponents = std::tuple<AnimRequestSet>;
};
