#include <app/EngineVerbs.h>

#include <string>

bool DeclareEngineVerbs(VerbRegistry& registry)
{
    VerbRegistrationScope scope(registry, "engine");

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

    return scope.Commit();
}
