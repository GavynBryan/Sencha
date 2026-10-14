#include "authoring/AbilityWorkbench.h"

#include "authoring/AbilityChecks.h"
#include "data/DataDocument.h"

#include <abilities/AbilityDefinitionData.h>
#include <effects/EffectDefinitionData.h>

#include <algorithm>

namespace
{
DataDocumentSetConfig ConfigFor(const std::vector<std::filesystem::path>& roots)
{
    return { .ContentRoot = roots.empty() ? std::filesystem::path{} : roots.front(),
             .Subtypes = { std::string(kAbilityDefinitionType), std::string(kEffectDefinitionType) } };
}
} // namespace

AbilityWorkbench::AbilityWorkbench(RuntimeAssets& assets, DataDocumentStore& store,
                                   std::vector<std::filesystem::path> roots,
                                   std::function<void(VocabularyCatalog&)> module)
    : Documents(store, ConfigFor(roots))
    , Assets(assets)
    , Roots(std::move(roots))
    , Module(std::move(module))
{
    RefreshNames();
    RefreshAssets();
}

void AbilityWorkbench::RefreshNames()
{
    Catalog = std::make_unique<VocabularyCatalog>();
    if (Module)
        Module(*Catalog);
    Catalog->DeclareContent(Assets);
    ++NamesGeneration;
}

void AbilityWorkbench::RefreshAssets()
{
    AbilityFiles.clear();
    for (ProjectFile& file : ScanProjectFiles(Roots))
        if (file.Subtype == kAbilityDefinitionType)
            AbilityFiles.push_back(std::move(file));
}

DataDocument* AbilityWorkbench::Ability()
{
    return Documents.ActiveOf(kAbilityDefinitionType);
}

const DataDocument* AbilityWorkbench::Ability() const
{
    const DataDocument* active = Documents.Active();
    return active != nullptr && active->Subtype() == kAbilityDefinitionType ? active : nullptr;
}

const JsonValue* AbilityWorkbench::AbilityData() const
{
    const DataDocument* ability = Ability();
    return ability != nullptr ? ability->Data() : nullptr;
}

bool AbilityWorkbench::Open(std::string_view path, std::string& error)
{
    const DataDocument* before = Ability();
    if (Documents.OpenOrFocus(path, error) == nullptr)
        return false;
    if (Ability() != before)
        Select({});
    return true;
}

const std::string& AbilityWorkbench::Selected()
{
    const JsonValue* data = AbilityData();
    if (!SelectedKey.empty() && (data == nullptr || !FindAbilityElement(*data, SelectedKey)))
        Select({});
    return SelectedKey;
}

void AbilityWorkbench::Select(std::string key, std::string focus)
{
    SelectedKey = std::move(key);
    FocusPath = std::move(focus);
}

std::optional<AbilityElement> AbilityWorkbench::SelectedElement()
{
    const std::string& key = Selected();
    const JsonValue* data = AbilityData();
    return key.empty() || data == nullptr ? std::nullopt : FindAbilityElement(*data, key);
}

void AbilityWorkbench::Reveal(std::string_view path)
{
    const JsonValue* data = AbilityData();
    const std::optional<AbilityElement> element = data != nullptr ? AbilityElementAt(*data, path) : std::nullopt;
    Select(element ? element->Key : std::string{}, std::string(path));
}

const std::vector<AbilityDiagnostic>& AbilityWorkbench::Diagnostics()
{
    const DataDocument* ability = Ability();
    if (ability == Checked && (ability == nullptr || ability->Revision() == CheckedRevision)
        && CheckedNames == NamesGeneration)
        return Found;
    Checked = ability;
    CheckedRevision = ability != nullptr ? ability->Revision() : 0;
    CheckedNames = NamesGeneration;
    Found.clear();
    if (const JsonValue* data = ability != nullptr ? ability->Data() : nullptr)
        Found = CheckAbilityData(*data, ability->VirtualPath(), Catalog->Names());
    return Found;
}

std::vector<Problem> AbilityWorkbench::Problems()
{
    const std::vector<AbilityDiagnostic>& diagnostics = Diagnostics();
    DataDocument* ability = Ability();
    if (ability == nullptr || ability->Data() == nullptr)
        return {};
    return AbilityProblems(diagnostics, *ability->Data(), Documents.RefOf(*ability));
}

bool AbilityWorkbench::Edit(const std::function<bool(JsonValue& data)>& change)
{
    DataDocument* ability = Ability();
    if (ability == nullptr)
        return false;
    JsonValue root = ability->CopyRoot();
    JsonValue* data = root.Find("data");
    if (data == nullptr || !change(*data))
        return false;
    Documents.ApplyEdit(*ability, std::move(root), DataEdit::Step);
    return true;
}
