#include <app/EngineVocabulary.h>
#include <app/LevelChangeVocabulary.h>

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

#include <string>

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

    VerbDefinition resume;
    resume.Name = std::string(kRuntimeResumeVerb);
    resume.DisplayName = "Resume";
    resume.Description = "Closes the application shell and resumes local play.";
    resume.Category = "Runtime";
    (void)scope.Declare(std::move(resume));

    VerbDefinition quit;
    quit.Name = std::string(kApplicationQuitVerb);
    quit.DisplayName = "Quit";
    quit.Description = "Asks the host to exit. The game's exit handler still sees the request.";
    quit.Category = "Application";
    (void)scope.Declare(std::move(quit));

    DeclareAnimRequestVerbs(scope);
    DeclareEffectVerbs(scope);
    DeclareAbilityVerbs(scope);
    DeclareAuthoredApi<GameplayTagOperations>(&scope, nullptr);
    DeclareActorTimeVerbs(scope);
    DeclareMovementVerbs(scope);
    DeclareAudioVerbs(scope);
    DeclareAttachmentVerbs(scope);
    DeclareCameraShakeVerbs(scope);
    DeclareCameraFovScaleVerbs(scope);
    DeclareNavigationVerbs(scope);
    DeclareResponseVocabulary(scope, eventScope);
    DeclareProjectileVocabulary(scope, eventScope);
    DeclareContactLineVocabulary(scope, queryScope);
    DeclareInteractUseVocabulary(scope, eventScope);
    DeclareAuthoredApi<SavedGameOperations>(&scope, nullptr);
    DeclareAuthoredApi<LevelChangeOperations>(&scope, nullptr);
    DeclareLevelChangeEvents(eventScope);
    DeclarePhysicsQueries(queryScope);
    DeclareNavigationQueries(queryScope);
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
