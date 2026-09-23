#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFacts.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimSelectorState.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class ConsoleRegistry;
class EngineSchedule;
class LoggingProvider;
class VerbDispatcher;
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

// What the clip event pass is composed with. Handed over rather than found:
// the pass invokes verbs, so the host names the dispatcher it may use.
struct AnimEventHost
{
    // Where crossed events are offered. Null still produces and records them,
    // each answered Unavailable.
    VerbDispatcher* Verbs = nullptr;
    // Where `anim.events.queue_capacity` is registered. Null keeps the
    // default capacity.
    ConsoleRegistry* Console = nullptr;
    // Whether this process presents a pose. Cosmetic events are produced only
    // where one is; a headless authority produces gameplay events alone.
    bool PresentsPose = true;
};

// Clip playback, then fact gathering, selection, content resolution and clip
// events, in that order. Rigs resolve through the World's AnimRigBindings,
// which RuntimeContent publishes; `logging` is where a rig that fails to bind
// is reported.
void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging = nullptr,
                              const AnimEventHost& events = {});
