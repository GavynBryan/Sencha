#include <app/EngineVocabularyBindings.h>

#include <app/LevelChangeVocabulary.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <attach/AttachmentVerbs.h>
#include <audio/AudioVerbs.h>
#include <camera/CameraFovScaleVerbs.h>
#include <camera/CameraShakeVerbs.h>
#include <effects/EffectVerbs.h>
#include <equipment/EquipmentVocabulary.h>
#include <gameplay_tags/GameplayTagVerbs.h>
#include <inventory/InventoryVocabulary.h>
#include <logic/InteractUse.h>
#include <logic/ResponseVerbs.h>
#include <movement/MovementVerbs.h>
#include <navigation/NavigationVerbs.h>
#include <line/ContactLineSystem.h>
#include <logic/ClaimVocabulary.h>
#include <logic/CounterVocabulary.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <projectile/ProjectileSystem.h>
#include <projectile/ProjectileVerbs.h>
#include <save/SavedGameVocabulary.h>
#include <save/SavedGames.h>
#include <time/ActorTimeVerbs.h>

#include <utility>

EngineVocabularyBindings::EngineVocabularyBindings(World& entities, const FixedSimulationLoop& clock,
                                                   VerbDispatcher& verbs)
    : Entities(entities)
    , Clock(clock)
    , Animation(std::make_unique<AnimRequestOperations>(entities, clock))
    , Effects(std::make_unique<EffectOperations>(entities, clock))
    , Time(std::make_unique<ActorTimeOperations>(entities, clock))
    , Abilities(std::make_unique<AbilityOperations>(entities))
    , Tags(std::make_unique<GameplayTagOperations>(entities))
    , Movement(std::make_unique<MovementOperations>(entities))
    , Navigation(std::make_unique<NavigationOperations>(entities))
    , Audio(std::make_unique<AudioOperations>(entities))
    , Attachments(std::make_unique<AttachmentOperations>(entities))
    , AttachedLights(std::make_unique<AttachedLightOperations>(entities, clock))
    , FovScale(std::make_unique<CameraFovScaleOperations>(entities, clock))
    , Shake(std::make_unique<CameraShakeOperations>(entities, clock, verbs))
    , Responses(std::make_unique<ResponseOperations>(entities, clock))
    , Perception(std::make_unique<PerceptionOperations>(entities))
    , Claims(std::make_unique<ClaimOperations>(entities))
{
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Animation));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Effects));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Time));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Abilities));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Tags));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Movement));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Navigation));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Audio));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Attachments));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *AttachedLights));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *FovScale));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Shake));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Responses));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Perception));
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Claims));
}

EngineVocabularyBindings::~EngineVocabularyBindings()
{
    Generated.clear();
}

void EngineVocabularyBindings::BindSavedGames(SavedGames& saves, VerbDispatcher& verbs)
{
    Saves = std::make_unique<SavedGameOperations>(saves);
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Saves));
}

void EngineVocabularyBindings::BindLevelChanges(LevelChange& changes, VerbDispatcher& verbs)
{
    LevelChanges = std::make_unique<LevelChangeOperations>(changes, Entities);
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *LevelChanges));
}

void EngineVocabularyBindings::BindCounters(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries, Logger& log,
                                            AuthoredEventDispatcher* events)
{
    Counters = std::make_unique<CounterOperations>(Entities, log, events);
    Generated.push_back(BindAuthoredApi(&verbs, &queries, *Counters));
}

void EngineVocabularyBindings::BindInventory(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries)
{
    Items = std::make_unique<InventoryOperations>(Entities, Clock);
    Generated.push_back(BindAuthoredApi(&verbs, &queries, *Items));
}

void EngineVocabularyBindings::BindEquipment(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries)
{
    Worn = std::make_unique<EquipmentOperations>(Entities, Clock);
    Generated.push_back(BindAuthoredApi(&verbs, &queries, *Worn));
}

void EngineVocabularyBindings::BindPerception(AuthoredQueryDispatcher& queries)
{
    Generated.push_back(BindAuthoredApi(nullptr, &queries, *Perception));
    Generated.push_back(BindAuthoredApi(nullptr, &queries, *Claims));
}

void EngineVocabularyBindings::BindNavigation(AuthoredQueryDispatcher& queries)
{
    Generated.push_back(BindAuthoredApi(nullptr, &queries, *Navigation));
}

void EngineVocabularyBindings::BindProjectiles(ProjectileSystem& projectiles, VerbDispatcher& verbs)
{
    Projectiles = std::make_unique<ProjectileOperations>(projectiles);
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Projectiles));
}

void EngineVocabularyBindings::BindContactLines(ContactLineSystem& lines, VerbDispatcher& verbs,
                                                AuthoredQueryDispatcher& queries)
{
    Generated.push_back(BindAuthoredApi(&verbs, &queries, lines));
}

void EngineVocabularyBindings::BindInteraction(const PhysicsWorld& physics, ResponsesSystem& responses,
                                               VerbDispatcher& verbs)
{
    Interaction = std::make_unique<InteractUseOperations>(Entities, physics, responses);
    Generated.push_back(BindAuthoredApi(&verbs, nullptr, *Interaction));
}

void EngineVocabularyBindings::BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries)
{
    Physics = std::make_unique<PhysicsQueryOperations>(physics);
    Generated.push_back(BindAuthoredApi(nullptr, &queries, *Physics));
}
