#include <authored/AuthoredApi.h>

#include <authored/WorldVocabulary.h>

AuthoredVocabularyScope::AuthoredVocabularyScope(World& world, std::string provider)
{
    if (VerbRegistry* verbs = FindVerbRegistry(world))
        Verbs.emplace(*verbs, provider);
    if (AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world))
        Queries.emplace(*queries, provider);
    if (AuthoredEventRegistry* events = FindAuthoredEventRegistry(world))
        Events.emplace(*events, provider);
}

void AuthoredVocabularyScope::DeclareVerb(VerbDefinition definition)
{
    if (Verbs)
        (void)Verbs->Declare(std::move(definition));
}

void AuthoredVocabularyScope::DeclareQuery(AuthoredQueryDefinition definition)
{
    if (Queries)
        (void)Queries->Declare(std::move(definition));
}

void AuthoredVocabularyScope::DeclareEvent(AuthoredEventDefinition definition)
{
    if (Events)
        (void)Events->Declare(std::move(definition));
}

bool AuthoredVocabularyScope::Commit()
{
    if (!Verbs || !Queries || !Events)
        return false;
    return CommitTogether(*Verbs, *Queries, *Events);
}
