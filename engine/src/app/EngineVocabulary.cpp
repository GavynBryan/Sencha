#include <app/EngineVocabulary.h>

#include <app/EngineVerbs.h>
#include <authored/WorldVocabulary.h>
#include <perception/PerceptionVocabulary.h>

bool InstallEngineVocabulary(World& world)
{
    VerbRegistry* verbs = FindVerbRegistry(world);
    const bool verbsDeclared = verbs != nullptr && DeclareEngineVerbs(*verbs);
    const bool perceptionDeclared = InstallPerceptionVocabulary(world);
    return verbsDeclared && perceptionDeclared;
}
