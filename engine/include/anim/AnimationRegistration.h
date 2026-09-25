#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFacts.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimRig.h>
#include <anim/AnimSelectorState.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class ConsoleRegistry;
class EngineSchedule;
class JobSystem;
class LoggingProvider;
class VerbDispatcher;
class World;

//=============================================================================
// Animation registration
//
// The animated-entity vocabulary. Which of these an entity carries is its tier:
// a rig brings its request set and content state, and is a Prop; fact storage
// brings its history and selector state, and makes it Simple or more. The
// decision log is opt-in on any tier. A clip played on its own is a one-layer
// rig whose behavior sets its speed, start and loop.
//=============================================================================

using AnimationComponents = ComponentSet<
    AnimRig,
    AnimRequestSet,
    AnimFacts,
    AnimFactsLarge,
    AnimFactHistory,
    AnimSelectorState,
    AnimContentState,
    AnimFlowState,
    AnimPoseState,
    AnimDecisionLog>;

void RegisterAnimationComponents(ComponentRegistrar& registrar);

// The layer tags every rig may name (anim.layer.base, anim.layer.upper,
// anim.layer.aim), an empty AnimFactProviders table for gameplay to bind
// into, and the AnimRequestJournal gameplay issues predicted requests through. Idempotent. Installed before a game's vocabulary hook, so a game's own
// layer tags and bindings land in the same registries.
void InstallAnimationVocabulary(World& world);

// The animation cvars over `world`'s installed limits: `anim.blend.override_cap`.
// Registered once by the process that owns `world`.
void RegisterAnimationCVars(ConsoleRegistry& console, World& world);

// What the animation systems are composed with. Handed over rather than
// found: the event pass invokes verbs, so the host names the dispatcher it may
// use, and the pose pass forks onto the host's workers.
struct AnimationHost
{
    // Where crossed events are offered. Null still produces and records them,
    // each answered Unavailable.
    VerbDispatcher* Verbs = nullptr;
    // Where `anim.events.queue_capacity` is registered. Null keeps the
    // default capacity.
    ConsoleRegistry* Console = nullptr;
    // Whether this process presents a pose. Cosmetic events are produced only
    // where one is, and only there are rigs posed; a headless authority
    // produces gameplay events alone.
    bool PresentsPose = true;
    // Where the pose pass forks. Null poses every entity inline.
    JobSystem* Jobs = nullptr;
};

// Clip playback, then fact gathering, selection, content resolution and clip
// events, in that order, and -- where a pose is presented -- posing after
// movement. Rigs resolve through the World's AnimRigBindings,
// which RuntimeContent publishes; `logging` is where a rig that fails to bind
// is reported.
void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging = nullptr,
                              const AnimationHost& host = {});
