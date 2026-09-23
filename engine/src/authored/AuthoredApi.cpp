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

    // Every step on every scope, not short-circuited: each batch that has
    // problems reports all of them, whichever catalog failed first.
    const bool begun = static_cast<int>(Verbs->BeginCommit()) & static_cast<int>(Queries->BeginCommit())
                     & static_cast<int>(Events->BeginCommit());
    if (!begun)
        return false;

    const bool ready = static_cast<int>(Verbs->Prepare()) & static_cast<int>(Queries->Prepare())
                     & static_cast<int>(Events->Prepare());
    if (!ready)
    {
        Verbs->Refuse();
        Queries->Refuse();
        Events->Refuse();
        return false;
    }

    // Nothing below can fail: every batch has been checked against the
    // catalog it goes into, and no catalog has changed since.
    Verbs->Publish();
    Queries->Publish();
    Events->Publish();
    return true;
}
