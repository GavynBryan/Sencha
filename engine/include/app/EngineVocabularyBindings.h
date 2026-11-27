#pragma once

#include <authored/AuthoredApi.h>
#include <authored/AuthoredBindingToken.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>

#include <memory>
#include <vector>

class AbilityOperations;
class GameplayTagOperations;
class ActorTimeOperations;
class AnimRequestOperations;
class AttachedLightOperations;
class AttachmentOperations;
class AuthoredEventDispatcher;
class AudioOperations;
class CameraFovScaleOperations;
class CameraShakeOperations;
class ContactLineSystem;
class CounterOperations;
class EffectOperations;
class EquipmentOperations;
class FixedSimulationLoop;
class InteractUseOperations;
class InventoryOperations;
class Logger;
class MovementOperations;
class NavigationOperations;
class PerceptionOperations;
class PhysicsQueries;
class ClaimOperations;
class ProjectileOperations;
class PhysicsWorld;
class ProjectileSystem;
class ResponseOperations;
class ResponsesSystem;
class LevelChange;
class LevelChangeOperations;
class SavedGameOperations;
class SavedGames;
class World;

//=============================================================================
// EngineVocabularyBindings
//
// The engine's own operations bound into one World's dispatchers: animation
// requests, effects, actor time, abilities, movement, navigation orders, triggers,
// audio and camera effects, counters, inventory, equipment, the perception and navigation queries, and the physics queries once a physics world answers them. The runtime and an editor preview both run a World
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

    // The perception.* queries, answered from this World's PerceptionFrame, and
    // the claims.* queries, from its ClaimBoard.
    void BindPerception(AuthoredQueryDispatcher& queries);

    // The navigation.* queries, answered from this World's agents' progress.
    void BindNavigation(AuthoredQueryDispatcher& queries);

    // physics.trace and physics.overlap answered from `physics`, which must outlive this.
    void BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries);

    // projectile.launch, answered by `projectiles`, which must outlive this.
    void BindProjectiles(ProjectileSystem& projectiles, VerbDispatcher& verbs);

    // line.extend, line.cut and the line.* queries, answered by `lines`, which must outlive this.
    void BindContactLines(ContactLineSystem& lines, VerbDispatcher& verbs, AuthoredQueryDispatcher& queries);

    // The save verbs, answered by `saves`, which must outlive this.
    void BindSavedGames(SavedGames& saves, VerbDispatcher& verbs);
    void BindLevelChanges(LevelChange& changes, VerbDispatcher& verbs);

    // counter.set, counter.add and counter.value; `events`, which may be null,
    // hears counter.changed and must outlive this.
    void BindCounters(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries, Logger& log,
                      AuthoredEventDispatcher* events);

    void BindInventory(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries);
    void BindEquipment(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries);

    // `physics` and `responses` must outlive this.
    void BindInteraction(const PhysicsWorld& physics, ResponsesSystem& responses, VerbDispatcher& verbs);

private:
    World& Entities;
    const FixedSimulationLoop& Clock;
    std::unique_ptr<AnimRequestOperations> Animation;
    std::unique_ptr<EffectOperations> Effects;
    std::unique_ptr<ActorTimeOperations> Time;
    std::unique_ptr<AbilityOperations> Abilities;
    std::unique_ptr<GameplayTagOperations> Tags;
    std::unique_ptr<MovementOperations> Movement;
    std::unique_ptr<NavigationOperations> Navigation;
    std::unique_ptr<AudioOperations> Audio;
    std::unique_ptr<AttachmentOperations> Attachments;
    std::unique_ptr<AttachedLightOperations> AttachedLights;
    std::unique_ptr<CameraFovScaleOperations> FovScale;
    std::unique_ptr<CameraShakeOperations> Shake;
    std::unique_ptr<ResponseOperations> Responses;
    std::unique_ptr<PerceptionOperations> Perception;
    std::unique_ptr<ClaimOperations> Claims;
    std::unique_ptr<ProjectileOperations> Projectiles;
    std::unique_ptr<PhysicsQueries> Physics;
    std::unique_ptr<InteractUseOperations> Interaction;
    std::unique_ptr<CounterOperations> Counters;
    std::unique_ptr<InventoryOperations> Items;
    std::unique_ptr<EquipmentOperations> Worn;
    std::unique_ptr<LevelChangeOperations> LevelChanges;
    std::unique_ptr<SavedGameOperations> Saves;
    std::vector<VerbBindingToken> Verbs;
    std::vector<AuthoredQueryBindingToken> Queries;
    std::vector<AuthoredApiBindings> Generated;
};
