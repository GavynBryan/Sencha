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

// Which of these an entity carries is its tier; see docs/gameplay/animation.md.
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

// Layer tags every rig may name, an empty AnimFactProviders table and the
// AnimRequestJournal. Idempotent; installed before a game's vocabulary hook so a
// game's own layer tags and bindings land in the same registries.
void InstallAnimationVocabulary(World& world);

// anim.blend.override_cap, anim.trace, anim.trace.export and anim.risk over
// `world`; see docs/gameplay/animation.md. Registered once by its owner.
void RegisterAnimationConsole(ConsoleRegistry& console, World& world);

// Handed over rather than found: the host names the dispatcher events may use and
// the workers the pose pass forks onto.
struct AnimationHost
{
    // Null still produces and records events, each answered Unavailable.
    VerbDispatcher* Verbs = nullptr;
    // Registers `anim.events.queue_capacity`; null keeps the default capacity.
    ConsoleRegistry* Console = nullptr;
    // Cosmetic events and posing happen only where a pose is presented; a headless
    // authority produces gameplay events alone.
    bool PresentsPose = true;
    // Null poses every entity inline.
    JobSystem* Jobs = nullptr;
};

// Fact gathering, selection, content resolution and events, in that order, then
// posing after movement where a pose is presented. Rigs resolve through the World's
// AnimRigBindings; `logging` reports a rig that fails to bind.
void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging = nullptr,
                              const AnimationHost& host = {});
