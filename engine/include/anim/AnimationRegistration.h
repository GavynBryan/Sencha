#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFacts.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimSelectorState.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class EngineSchedule;
class LoggingProvider;
class World;

//=============================================================================
// Animation registration
//
// The animated-entity vocabulary. Which of these an entity carries is its tier:
// a rig brings its request set and content state, and is a Prop; fact storage
// brings its history and selector state, and makes it Simple or more. The
// decision log is opt-in on any tier.
//
// The clip player is not here. It is a render-facing component the renderer's
// vocabulary already names, and the animation runtime will replace what it
// does rather than extend it.
//=============================================================================

using AnimationComponents = ComponentSet<
    AnimRig,
    AnimRequestSet,
    AnimFacts,
    AnimFactsLarge,
    AnimFactHistory,
    AnimSelectorState,
    AnimContentState,
    AnimDecisionLog>;

void RegisterAnimationComponents(ComponentRegistrar& registrar);

// The layer tags every rig may name (anim.layer.base, anim.layer.upper,
// anim.layer.aim) and an empty AnimFactProviders table for gameplay to bind
// into. Idempotent. Installed before a game's vocabulary hook, so a game's own
// layer tags and bindings land in the same registries.
void InstallAnimationVocabulary(World& world);

// Clip playback, then fact gathering, selection and content resolution, in
// that order. Rigs resolve through the World's
// AnimRigBindings, which RuntimeContent publishes; `logging` is where a rig
// that fails to bind is reported.
void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging = nullptr);
