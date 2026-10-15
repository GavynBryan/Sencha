#pragma once

#include <authored/AuthoredBindingToken.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>

#include <memory>
#include <vector>

class AbilityOperations;
class ActorTimeOperations;
class AnimRequestOperations;
class AudioOperations;
class EffectOperations;
class FixedSimulationLoop;
class MovementOperations;
class PhysicsQueries;
class PhysicsWorld;
class World;

//=============================================================================
// EngineVocabularyBindings
//
// The engine's own operations bound into one World's dispatchers: animation
// requests, effects, actor time, abilities, held movement and audio, the perception
// queries, and the physics queries once a physics world answers them. The runtime and an editor preview both run a World
// this way, so both run the same behaviour behind the same names. The bindings
// are released before the operations they call.
//=============================================================================
class EngineVocabularyBindings
{
public:
    EngineVocabularyBindings(World& entities, const FixedSimulationLoop& clock, VerbDispatcher& verbs);
    ~EngineVocabularyBindings();

    EngineVocabularyBindings(const EngineVocabularyBindings&) = delete;
    EngineVocabularyBindings& operator=(const EngineVocabularyBindings&) = delete;

    // The perception.* queries, answered from this World's PerceptionFrame.
    void BindPerception(AuthoredQueryDispatcher& queries);

    // physics.trace and physics.overlap answered from `physics`, which must outlive this.
    void BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries);

private:
    const World& Entities;
    std::unique_ptr<AnimRequestOperations> Animation;
    std::unique_ptr<EffectOperations> Effects;
    std::unique_ptr<ActorTimeOperations> Time;
    std::unique_ptr<AbilityOperations> Abilities;
    std::unique_ptr<MovementOperations> Movement;
    std::unique_ptr<AudioOperations> Audio;
    std::unique_ptr<PhysicsQueries> Physics;
    std::vector<VerbBindingToken> Verbs;
    std::vector<AuthoredQueryBindingToken> Queries;
};
