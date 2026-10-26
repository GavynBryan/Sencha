#include <app/EngineVocabulary.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <attach/AttachmentVerbs.h>
#include <audio/AudioVerbs.h>
#include <authored/WorldVocabulary.h>
#include <effects/EffectVerbs.h>
#include <logic/ResponseVerbs.h>
#include <movement/MovementVerbs.h>
#include <navigation/NavigationVerbs.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
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
    DeclareActorTimeVerbs(scope);
    DeclareMovementVerbs(scope);
    DeclareAudioVerbs(scope);
    DeclareAttachmentVerbs(scope);
    DeclareNavigationVerbs(scope);
    DeclareResponseVocabulary(scope, eventScope);
    DeclarePhysicsQueries(queryScope);
    DeclareNavigationQueries(queryScope);

    const bool engineDeclared = CommitTogether(scope, queryScope, eventScope);
    const bool perceptionDeclared = InstallPerceptionVocabulary(world);
    return engineDeclared && perceptionDeclared;
}
