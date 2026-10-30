#pragma once

#include <authored/AuthoredBindingToken.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>

#include <memory>
#include <vector>

class AbilityOperations;
class ActorTimeOperations;
class AnimRequestOperations;
class AttachmentOperations;
class AudioOperations;
class CameraFovScaleOperations;
class CameraShakeOperations;
class EffectOperations;
class FixedSimulationLoop;
class InteractUseOperations;
class MovementOperations;
class NavigationOperations;
class PhysicsQueries;
class PhysicsWorld;
class ProjectileSystem;
class ResponseOperations;
class ResponsesSystem;
class World;

//=============================================================================
// EngineVocabularyBindings
//
// The engine's own operations bound into one World's dispatchers: animation
// requests, effects, actor time, abilities, movement, navigation orders, triggers,
// audio and camera effects, the perception and navigation queries, and the physics queries once a physics world answers them. The runtime and an editor preview both run a World
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

    // The navigation.* queries, answered from this World's agents' progress.
    void BindNavigation(AuthoredQueryDispatcher& queries);

    // physics.trace and physics.overlap answered from `physics`, which must outlive this.
    void BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries);

    // projectile.launch, answered by `projectiles`, which must outlive this.
    void BindProjectiles(ProjectileSystem& projectiles, VerbDispatcher& verbs);

    // `physics` and `responses` must outlive this.
    void BindInteraction(const PhysicsWorld& physics, ResponsesSystem& responses, VerbDispatcher& verbs);

private:
    const World& Entities;
    std::unique_ptr<AnimRequestOperations> Animation;
    std::unique_ptr<EffectOperations> Effects;
    std::unique_ptr<ActorTimeOperations> Time;
    std::unique_ptr<AbilityOperations> Abilities;
    std::unique_ptr<MovementOperations> Movement;
    std::unique_ptr<NavigationOperations> Navigation;
    std::unique_ptr<AudioOperations> Audio;
    std::unique_ptr<AttachmentOperations> Attachments;
    std::unique_ptr<CameraFovScaleOperations> FovScale;
    std::unique_ptr<CameraShakeOperations> Shake;
    std::unique_ptr<ResponseOperations> Responses;
    std::unique_ptr<PhysicsQueries> Physics;
    std::unique_ptr<InteractUseOperations> Interaction;
    std::vector<VerbBindingToken> Verbs;
    std::vector<AuthoredQueryBindingToken> Queries;
};
