#include <app/EngineVocabulary.h>
#include <app/LevelChangeVocabulary.h>
#include <app/ShellVerbs.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <attach/AttachmentVerbs.h>
#include <audio/AudioVerbs.h>
#include <camera/CameraFovScaleVerbs.h>
#include <camera/CameraShakeVerbs.h>
#include <authored/AuthoredApi.h>
#include <authored/WorldVocabulary.h>
#include <effects/EffectVerbs.h>
#include <logic/ClaimVocabulary.h>
#include <equipment/EquipmentKit.h>
#include <equipment/EquipmentVocabulary.h>
#include <gameplay_tags/GameplayTagVerbs.h>
#include <inventory/InventoryVocabulary.h>
#include <logic/CounterVocabulary.h>
#include <save/SavedGameVocabulary.h>
#include <logic/InteractUse.h>
#include <logic/ResponseVerbs.h>
#include <movement/MovementRegistration.h>
#include <movement/MovementVerbs.h>
#include <navigation/NavigationVerbs.h>
#include <line/ContactLineSystem.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <projectile/ProjectileVerbs.h>
#include <time/ActorTimeVerbs.h>


bool InstallEngineVocabulary(World& world)
{
    VerbRegistry* verbs = FindVerbRegistry(world);
    AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world);
    AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
    if (verbs == nullptr || queries == nullptr || events == nullptr)
        return false;
    VerbRegistrationScope scope(*verbs, "engine");
    AuthoredQueryRegistrationScope queryScope(*queries, "engine");
    AuthoredEventRegistrationScope eventScope(*events, "engine");

    DeclareAuthoredApi<RuntimeResumeOperation>(&scope, nullptr);
    DeclareAuthoredApi<ApplicationQuitOperation>(&scope, nullptr);

    DeclareAuthoredApi<AnimRequestOperations>(&scope, nullptr);
    DeclareAuthoredApi<EffectOperations>(&scope, nullptr);
    DeclareAuthoredApi<AbilityOperations>(&scope, nullptr);
    DeclareAuthoredApi<GameplayTagOperations>(&scope, nullptr);
    DeclareAuthoredApi<ActorTimeOperations>(&scope, nullptr);
    DeclareAuthoredApi<MovementOperations>(&scope, nullptr);
    DeclareAuthoredApi<AudioOperations>(&scope, nullptr);
    DeclareAuthoredApi<AttachmentOperations>(&scope, nullptr);
    DeclareAuthoredApi<AttachedLightOperations>(&scope, nullptr);
    DeclareAuthoredApi<CameraShakeOperations>(&scope, nullptr);
    DeclareAuthoredApi<CameraFovScaleOperations>(&scope, nullptr);
    DeclareAuthoredApi<NavigationOperations>(&scope, nullptr);
    DeclareResponseVocabulary(scope, eventScope);
    DeclareProjectileVocabulary(scope, eventScope);
    DeclareAuthoredApi<ContactLineSystem>(&scope, &queryScope);
    DeclareInteractUseVocabulary(scope, eventScope);
    DeclareAuthoredApi<SavedGameOperations>(&scope, nullptr);
    DeclareAuthoredApi<LevelChangeOperations>(&scope, nullptr);
    DeclareLevelChangeEvents(eventScope);
    DeclareAuthoredApi<PhysicsQueryOperations>(nullptr, &queryScope);
    DeclareAuthoredApi<NavigationOperations>(nullptr, &queryScope);
    DeclareCounterVocabulary(scope, queryScope, eventScope);
    DeclareInventoryVocabulary(scope, queryScope);
    DeclareEquipmentVocabulary(scope, queryScope);

    const bool engineDeclared = CommitTogether(scope, queryScope, eventScope);
    const bool perceptionDeclared = InstallPerceptionVocabulary(world);
    const bool claimsDeclared = InstallClaimVocabulary(world);
    return engineDeclared && perceptionDeclared && claimsDeclared;
}

void InstallEveryKitVocabulary(World& world)
{
    InstallMovementVocabulary(world);
    InstallEquipmentVocabulary(world);
}
