#pragma once

#include <authored/VerbBinding.h>
#include <authored/VerbRegistry.h>
#include <ecs/World.h>

#include <cstddef>
#include <string>
#include <vector>

class Game;

//=============================================================================
// VocabularyCatalog
//
// What an author can name, and whether what they named resolves: the engine's
// verbs, a loaded game module's verbs, and each record of a binding asset held
// against them.
//
// A metadata World and nothing else. It carries the catalog and the vocabulary
// registries a module's hook is entitled to fill, and no dispatcher -- there is
// nothing here that can run, which is the point: the previewer offers and
// checks a vocabulary without acquiring the ability to quit the application
// because a module declared the name.
//
// GUI-free, so a test drives it the way the panel does. The World must be
// destroyed before the module whose hook filled it is unmapped; the owner
// orders its members accordingly.
//=============================================================================
class VocabularyCatalog
{
public:
    VocabularyCatalog();

    // Replays the module's declarations into this catalog. Registration only:
    // the module's runtime is never started. Errors the module's declarations
    // produced are kept for Errors().
    void InstallModuleVocabulary(Game& game);

    [[nodiscard]] const VerbRegistry& Verbs() const;
    [[nodiscard]] std::span<const std::string> Errors() const { return Diagnostics; }

    struct VerbRow
    {
        std::string Name;
        std::string DisplayName;
        std::string Category;
        std::string Provider;
        std::size_t ArgumentCount = 0;
    };
    // Every live verb, in catalog order.
    [[nodiscard]] std::vector<VerbRow> ListVerbs() const;

    struct BindingRow
    {
        std::string Key;
        std::string VerbName;
        bool Resolved = false;
        // The first reason it did not, when it did not.
        std::string Error;
    };
    // Compiles each record against this catalog and reports the outcome. The
    // records are untouched: an unresolved binding is content to inspect.
    [[nodiscard]] std::vector<BindingRow> Inspect(const VerbBindingLibrary& library) const;

private:
    World Metadata;
    std::vector<std::string> Diagnostics;
};
