#pragma once

#include "authoring/AbilityElements.h"
#include "data/DataDocumentSet.h"
#include "problems/ProblemList.h"
#include "project/ProjectFiles.h"
#include "vocabulary/VocabularyCatalog.h"

#include <abilities/AbilityDeclaration.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct RuntimeAssets;

// The Ability Workbench's state, GUI-free (docs/editor/ability-workbench.md): the
// project's abilities, the one open, which of its elements is selected, what it
// means against the project's names, and the edits made to it -- each one step
// of the document's journal.
class AbilityWorkbench
{
public:
    // `module` adds a loaded game module's names; `roots` are the project's content roots.
    AbilityWorkbench(RuntimeAssets& assets, DataDocumentStore& store, std::vector<std::filesystem::path> roots,
                     std::function<void(VocabularyCatalog&)> module);

    DataDocumentSet Documents;

    // The names abilities are checked against, declared afresh: after content or
    // the module's vocabulary changed.
    void RefreshNames();
    [[nodiscard]] const VocabularyCatalog& Names() const { return *Catalog; }

    void RefreshAssets();
    // The project's data assets of `subtype`, by asset path.
    [[nodiscard]] std::vector<ProjectFile> FilesOf(std::string_view subtype) const;
    [[nodiscard]] std::vector<ProjectFile> Abilities() const;

    // The ability being authored: the last ability document made active, which
    // stays open on the strip while another document, such as an effect, is shown.
    [[nodiscard]] DataDocument* Ability();
    [[nodiscard]] const DataDocument* Ability() const;
    [[nodiscard]] const JsonValue* AbilityData() const;
    [[nodiscard]] bool ShowingAbility() const;
    bool Open(std::string_view path, std::string& error);
    // Takes the active document as the ability being authored when it is one.
    void FollowActive();

    // The selected element's key, empty for the ability as a whole; kept only
    // while an element answers to it.
    [[nodiscard]] const std::string& Selected();
    void Select(std::string key, std::string focus = {});
    [[nodiscard]] std::optional<AbilityElement> SelectedElement();
    // A property path to bring forward in the inspector: a problem's field.
    [[nodiscard]] const std::string& Focus() const { return FocusPath; }
    // The element a problem at `path` is about, selected, with its field brought forward.
    void Reveal(std::string_view path);

    [[nodiscard]] const std::vector<AbilityDiagnostic>& Diagnostics();
    // What the working data compiled to, as far as it compiled.
    [[nodiscard]] const AbilityDeclaration& Declaration();
    [[nodiscard]] std::vector<Problem> Problems();

    // `change` made to the open ability's data as one step; false, and nothing
    // changed, when it refuses or there is no ability open.
    bool Edit(const std::function<bool(JsonValue& data)>& change);

private:
    RuntimeAssets& Assets;
    std::vector<std::filesystem::path> Roots;
    std::function<void(VocabularyCatalog&)> Module;
    std::unique_ptr<VocabularyCatalog> Catalog;
    std::vector<ProjectFile> DataFiles;
    std::string AbilityPath;
    std::string SelectedKey;
    std::string FocusPath;

    // What Diagnostics() was computed from.
    const DataDocument* Checked = nullptr;
    std::uint64_t CheckedRevision = 0;
    std::uint64_t CheckedNames = 0;
    std::uint64_t NamesGeneration = 1;
    std::vector<AbilityDiagnostic> Found;
    AbilityDeclaration Compiled;

    void Check();
};
