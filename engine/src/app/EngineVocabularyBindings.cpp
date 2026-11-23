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
#include <physics/PhysicsQueries.h>
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
{
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeAnimRequest>(catalog.Find(kAnimRequestVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimCancel>(catalog.Find(kAnimCancelVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimHold>(catalog.Find(kAnimHoldLease), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimPresent>(catalog.Find(kAnimPresentVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeEffectApply>(catalog.Find(kEffectApplyVerb), *Effects));
    Verbs.push_back(verbs.Bind<&InvokeEffectHold>(catalog.Find(kEffectHoldLease), *Effects));
    Verbs.push_back(verbs.Bind<&InvokeTimeScale>(catalog.Find(kTimeScaleLease), *Time));
    Verbs.push_back(verbs.Bind<&InvokeAbilityActivate>(catalog.Find(kAbilityActivateVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityCancel>(catalog.Find(kAbilityCancelVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityGrant>(catalog.Find(kAbilityGrantVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityRevoke>(catalog.Find(kAbilityRevokeVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeTagsGrant>(catalog.Find(kTagsGrantVerb), *Tags));
    Verbs.push_back(verbs.Bind<&InvokeTagsRevoke>(catalog.Find(kTagsRevokeVerb), *Tags));
    Verbs.push_back(verbs.Bind<&InvokeMovementBurst>(catalog.Find(kMovementBurstLease), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementTether>(catalog.Find(kMovementTetherLease), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementMode>(catalog.Find(kMovementModeLease), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementLaunch>(catalog.Find(kMovementLaunchVerb), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementImpulse>(catalog.Find(kMovementImpulseVerb), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeNavigationMoveTo>(catalog.Find(kNavigationMoveToVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeNavigationApproach>(catalog.Find(kNavigationApproachVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeNavigationStop>(catalog.Find(kNavigationStopVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeAudioPlay>(catalog.Find(kAudioPlayVerb), *Audio));
    Verbs.push_back(verbs.Bind<&InvokeAudioLoop>(catalog.Find(kAudioLoopLease), *Audio));
    Verbs.push_back(verbs.Bind<&InvokePresentationAttach>(catalog.Find(kPresentationAttachLease), *Attachments));
    Verbs.push_back(verbs.Bind<&InvokePresentationLight>(catalog.Find(kPresentationLightLease), *AttachedLights));
    Verbs.push_back(verbs.Bind<&InvokeCameraFovScale>(catalog.Find(kCameraFovScaleVerb), *FovScale));
    Verbs.push_back(verbs.Bind<&InvokeCameraFovScaleHold>(catalog.Find(kCameraFovScaleHoldLease), *FovScale));
    Verbs.push_back(verbs.Bind<&InvokeCameraShake>(catalog.Find(kCameraShakeVerb), *Shake));
    Verbs.push_back(verbs.Bind<&InvokeCameraShakeHold>(catalog.Find(kCameraShakeHoldLease), *Shake));
    Verbs.push_back(verbs.Bind<&InvokeResponsesEnable>(catalog.Find(kResponsesEnableVerb), *Responses));
    Verbs.push_back(verbs.Bind<&InvokeResponsesDisable>(catalog.Find(kResponsesDisableVerb), *Responses));
    Verbs.push_back(verbs.Bind<&InvokeTriggerInteract>(catalog.Find(kTriggerInteractVerb), *Responses));
    Verbs.push_back(BindPerceptionVerbs(verbs, entities));
    Verbs.push_back(BindClaimVerbs(verbs, entities));
}

EngineVocabularyBindings::~EngineVocabularyBindings()
{
    Queries.clear();
    Verbs.clear();
}

void EngineVocabularyBindings::BindSavedGames(SavedGames& saves, VerbDispatcher& verbs)
{
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeSaveWrite>(catalog.Find(kSaveWriteVerb), saves));
    Verbs.push_back(verbs.Bind<&InvokeSaveRestore>(catalog.Find(kSaveRestoreVerb), saves));
    Verbs.push_back(verbs.Bind<&InvokeSaveDelete>(catalog.Find(kSaveDeleteVerb), saves));
    Verbs.push_back(verbs.Bind<&InvokeSaveCopy>(catalog.Find(kSaveCopyVerb), saves));
}

void EngineVocabularyBindings::BindLevelChanges(LevelChange& changes, VerbDispatcher& verbs)
{
    Verbs.push_back(verbs.Bind<&InvokeLevelChange>(verbs.Registry().Find(kLevelChangeVerb), changes));
}

void EngineVocabularyBindings::BindCounters(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries, Logger& log,
                                            AuthoredEventDispatcher* events)
{
    Counters = std::make_unique<CounterOperations>(Entities, log, events);
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeCounterSet>(catalog.Find(kCounterSetVerb), *Counters));
    Verbs.push_back(verbs.Bind<&InvokeCounterAdd>(catalog.Find(kCounterAddVerb), *Counters));
    Queries.push_back(BindCounterQuery(queries, std::as_const(Entities)));
}

void EngineVocabularyBindings::BindInventory(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries)
{
    Items = std::make_unique<InventoryOperations>(Entities, Clock);
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeInventoryTake>(catalog.Find(kInventoryTakeVerb), *Items));
    Verbs.push_back(verbs.Bind<&InvokeInventoryGive>(catalog.Find(kInventoryGiveVerb), *Items));
    Verbs.push_back(verbs.Bind<&InvokeInventoryTransfer>(catalog.Find(kInventoryTransferVerb), *Items));
    Verbs.push_back(verbs.Bind<&InvokeInventoryDrop>(catalog.Find(kInventoryDropVerb), *Items));
    Verbs.push_back(verbs.Bind<&InvokeInventoryUse>(catalog.Find(kInventoryUseVerb), *Items));
    Queries.push_back(BindInventoryQuery(queries, std::as_const(Entities)));
}

void EngineVocabularyBindings::BindEquipment(VerbDispatcher& verbs, AuthoredQueryDispatcher& queries)
{
    Worn = std::make_unique<EquipmentOperations>(Entities, Clock);
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeEquipmentEquip>(catalog.Find(kEquipmentEquipVerb), *Worn));
    Verbs.push_back(verbs.Bind<&InvokeEquipmentUnequip>(catalog.Find(kEquipmentUnequipVerb), *Worn));
    Verbs.push_back(verbs.Bind<&InvokeEquipmentCycle>(catalog.Find(kEquipmentCycleVerb), *Worn));
    for (AuthoredQueryBindingToken& token : BindEquipmentQueries(queries, std::as_const(Entities)))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindPerception(AuthoredQueryDispatcher& queries)
{
    for (AuthoredQueryBindingToken& token : BindPerceptionQueries(queries, Entities))
        Queries.push_back(std::move(token));
    for (AuthoredQueryBindingToken& token : BindClaimQueries(queries, Entities))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindNavigation(AuthoredQueryDispatcher& queries)
{
    for (AuthoredQueryBindingToken& token : BindNavigationQueries(queries, Entities))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindProjectiles(ProjectileSystem& projectiles, VerbDispatcher& verbs)
{
    Verbs.push_back(verbs.Bind<&InvokeProjectileLaunch>(verbs.Registry().Find(kProjectileLaunchVerb), projectiles));
}

void EngineVocabularyBindings::BindContactLines(ContactLineSystem& lines, VerbDispatcher& verbs,
                                                AuthoredQueryDispatcher& queries)
{
    Verbs.push_back(verbs.Bind<&InvokeLineExtend>(verbs.Registry().Find(kLineExtendLease), lines));
    Verbs.push_back(verbs.Bind<&InvokeLineCut>(verbs.Registry().Find(kLineCutVerb), lines));
    Verbs.push_back(verbs.Bind<&InvokeLineRetract>(verbs.Registry().Find(kLineRetractVerb), lines));
    for (AuthoredQueryBindingToken& token : BindContactLineQueries(queries, lines))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindInteraction(const PhysicsWorld& physics, ResponsesSystem& responses,
                                               VerbDispatcher& verbs)
{
    Interaction = std::make_unique<InteractUseOperations>(Entities, physics, responses);
    Verbs.push_back(verbs.Bind<&InvokeInteractUse>(verbs.Registry().Find(kInteractUseVerb), *Interaction));
}

void EngineVocabularyBindings::BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries)
{
    Physics = std::make_unique<PhysicsQueries>(physics);
    Queries.push_back(queries.Bind<&EvaluatePhysicsTrace>(queries.Registry().Find(kPhysicsTraceQuery),
                                                          std::as_const(*Physics)));
    Queries.push_back(queries.Bind<&EvaluatePhysicsOverlap>(queries.Registry().Find(kPhysicsOverlapQuery),
                                                            std::as_const(*Physics)));
    Queries.push_back(queries.Bind<&EvaluatePhysicsClearPath>(queries.Registry().Find(kPhysicsClearPathQuery),
                                                              std::as_const(*Physics)));
}
