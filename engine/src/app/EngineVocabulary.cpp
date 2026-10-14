#include <app/EngineVocabulary.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <authored/WorldVocabulary.h>
#include <effects/EffectVerbs.h>
#include <physics/PhysicsAuthoredQueries.h>

#include <string>

bool DeclareEngineVocabulary(World& world)
{
    VerbRegistry* verbs = FindVerbRegistry(world);
    AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world);
    if (verbs == nullptr || queries == nullptr)
        return false;
    VerbRegistrationScope scope(*verbs, "engine");
    AuthoredQueryRegistrationScope queryScope(*queries, "engine");

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
    DeclarePhysicsQueries(queryScope);

    return CommitTogether(scope, queryScope);
}
