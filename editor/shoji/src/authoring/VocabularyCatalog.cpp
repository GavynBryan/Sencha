#include "authoring/VocabularyCatalog.h"

#include <app/EngineVerbs.h>
#include <app/Game.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/WorldVocabulary.h>
#include <movement/MovementRegistration.h>

#include <utility>

VocabularyCatalog::VocabularyCatalog()
{
    // The registries a module's hook may fill: tags, attributes, abilities,
    // modes -- what Kyusu gives a document, for the same reason. A module
    // declaring a tag into a World with no tag registry would be told nothing.
    RegisterMovement(Metadata);
    InstallAuthoredVocabulary(Metadata);
    (void)DeclareEngineVerbs(*FindVerbRegistry(Metadata));
    TakeInstallationErrors();
}

void VocabularyCatalog::InstallModuleVocabulary(Game& game)
{
    game.OnRegisterVocabulary(Metadata);
    TakeInstallationErrors();
}

void VocabularyCatalog::TakeInstallationErrors()
{
    for (std::string& error : AuthoredInstallationErrors(Metadata))
        Diagnostics.push_back(std::move(error));
    FindVerbRegistry(Metadata)->ClearInstallationErrors();
    FindAuthoredQueryRegistry(Metadata)->ClearInstallationErrors();
    FindAuthoredEventRegistry(Metadata)->ClearInstallationErrors();
}

const VerbRegistry& VocabularyCatalog::Verbs() const
{
    return *FindVerbRegistry(Metadata);
}

std::vector<VocabularyCatalog::VerbRow> VocabularyCatalog::ListVerbs() const
{
    const VerbRegistry& verbs = Verbs();
    std::vector<VerbRow> rows;
    for (const VerbId id : verbs.Live())
    {
        const VerbDefinition& definition = *verbs.Get(id);
        rows.push_back(VerbRow{
            .Name = definition.Name,
            .DisplayName = definition.DisplayName,
            .Category = definition.Category,
            .Provider = std::string(verbs.Provider(id)),
            .ArgumentCount = definition.Arguments.Children.size(),
        });
    }
    return rows;
}

std::vector<VocabularyCatalog::QueryRow> VocabularyCatalog::ListQueries() const
{
    const AuthoredQueryRegistry& queries = *FindAuthoredQueryRegistry(Metadata);
    std::vector<QueryRow> rows;
    for (const AuthoredQueryId id : queries.Live())
    {
        const AuthoredQueryDefinition& definition = *queries.Get(id);
        rows.push_back(QueryRow{
            .Name = definition.Name,
            .DisplayName = definition.DisplayName,
            .Provider = std::string(queries.Provider(id)),
            .ArgumentCount = definition.Arguments.Children.size(),
        });
    }
    return rows;
}

std::vector<VocabularyCatalog::EventRow> VocabularyCatalog::ListEvents() const
{
    const AuthoredEventRegistry& events = *FindAuthoredEventRegistry(Metadata);
    std::vector<EventRow> rows;
    for (const AuthoredEventId id : events.Live())
    {
        const AuthoredEventDefinition& definition = *events.Get(id);
        rows.push_back(EventRow{
            .Name = definition.Name,
            .DisplayName = definition.DisplayName,
            .SourceComponent = definition.SourceComponent,
            .Provider = std::string(events.Provider(id)),
            .PayloadCount = definition.Payload.Children.size(),
        });
    }
    return rows;
}

std::vector<VocabularyCatalog::BindingRow>
VocabularyCatalog::Inspect(const VerbBindingLibrary& library,
                           const AssetRegistry* assets,
                           const DataAssetCache* dataAssets) const
{
    VerbBindingEnvironment environment = MakeVerbBindingEnvironment(Metadata);
    environment.Assets = assets;
    environment.DataAssets = dataAssets;
    std::vector<BindingRow> rows;
    rows.reserve(library.Bindings.size());
    for (const VerbBindingDesc& desc : library.Bindings)
    {
        BindingRow row;
        row.Key = desc.Key;
        row.VerbName = desc.VerbName;
        std::vector<std::string> errors;
        CompiledVerbBinding compiled;
        row.Resolved = CompileVerbBinding(desc, environment, compiled, errors);
        row.ReferencesChecked = !row.Resolved || compiled.ReferencesChecked;
        if (!row.Resolved && !errors.empty())
            row.Error = errors.front();
        rows.push_back(std::move(row));
    }
    return rows;
}
