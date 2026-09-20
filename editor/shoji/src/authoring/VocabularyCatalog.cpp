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
    VerbRegistry& verbs = InstallVerbRegistry(Metadata);
    (void)DeclareEngineVerbs(verbs);
    for (const std::string& error : verbs.InstallationErrors())
        Diagnostics.push_back(error);
    verbs.ClearInstallationErrors();
}

void VocabularyCatalog::InstallModuleVocabulary(Game& game)
{
    game.OnRegisterVocabulary(Metadata);
    VerbRegistry& verbs = InstallVerbRegistry(Metadata);
    for (const std::string& error : verbs.InstallationErrors())
        Diagnostics.push_back(error);
    verbs.ClearInstallationErrors();
}

const VerbRegistry& VocabularyCatalog::Verbs() const
{
    return *FindVerbRegistry(Metadata);
}

std::vector<VocabularyCatalog::VerbRow> VocabularyCatalog::ListVerbs() const
{
    const VerbRegistry& verbs = Verbs();
    std::vector<VerbRow> rows;
    for (const VerbId id : verbs.LiveVerbs())
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

std::vector<VocabularyCatalog::BindingRow>
VocabularyCatalog::Inspect(const VerbBindingLibrary& library) const
{
    const VerbBindingEnvironment environment = MakeVerbBindingEnvironment(Metadata);
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
        if (!row.Resolved && !errors.empty())
            row.Error = errors.front();
        rows.push_back(std::move(row));
    }
    return rows;
}
