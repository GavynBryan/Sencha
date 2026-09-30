#include "MaterialDocumentSet.h"

#include "documents/DocumentSourceSet.h"

#include <algorithm>
#include <system_error>
#include <utility>

MaterialDocumentSet::MaterialDocumentSet(DocumentSourceSet& sources, ResidentPush pushResident)
    : Sources(sources)
    , PushResident(std::move(pushResident))
{
    Sources.AddSource(*this);
}

MaterialDocumentSet::~MaterialDocumentSet()
{
    Sources.RemoveSource(*this);
}

void MaterialDocumentSet::Observe(MaterialEditTab& tab)
{
    // Looked up by path when it fires rather than captured: the tab's path
    // changes when its file is renamed.
    tab.Commands.SetExecuteObserver([this, &tab] { Sources.Record(RefOf(tab)); });
    tab.Commands.SetClearObserver([this, &tab] { Sources.ForgetDocument(RefOf(tab)); });
}

MaterialEditTab* MaterialDocumentSet::OpenOrFocus(std::string virtualPath, std::string filePath, std::string* error)
{
    if (const std::size_t index = IndexOf(virtualPath); index < List.size())
    {
        ActiveTab = index;
        return List[index].get();
    }

    auto tab = std::make_unique<MaterialEditTab>();
    if (!tab->Session.Open(std::move(virtualPath), std::move(filePath), error))
        return nullptr;
    Observe(*tab);
    List.push_back(std::move(tab));
    ActiveTab = List.size() - 1;
    return List.back().get();
}

bool MaterialDocumentSet::Close(std::size_t index, DirtyDisposition disposition, std::string& error)
{
    if (index >= List.size())
    {
        error = "That material is not open.";
        return false;
    }
    MaterialEditTab& tab = *List[index];
    if (tab.Session.IsDirty())
    {
        if (disposition == DirtyDisposition::Refuse)
        {
            error = "'" + tab.Session.VirtualPath() + "' has unsaved changes.";
            return false;
        }
        if (disposition == DirtyDisposition::Save)
        {
            const DocumentSaveResult saved = Sources.Save(RefOf(tab));
            if (saved.Status == DocumentSaveStatus::Conflict || saved.Status == DocumentSaveStatus::Failed)
            {
                error = saved.Status == DocumentSaveStatus::Conflict
                    ? "'" + tab.Session.VirtualPath() + "' changed on disk since it was read; settle that first."
                    : saved.Error;
                return false;
            }
        }
        else if (PushResident)
        {
            // The resident material showed the working version; it goes back
            // to the file's before the tab does.
            PushResident(tab, tab.Session.Saved());
        }
    }
    Sources.ForgetDocument(RefOf(tab));
    List.erase(List.begin() + static_cast<std::ptrdiff_t>(index));
    if (ActiveTab >= List.size())
        ActiveTab = List.empty() ? 0 : List.size() - 1;
    return true;
}

MaterialEditTab* MaterialDocumentSet::Active()
{
    return ActiveTab < List.size() ? List[ActiveTab].get() : nullptr;
}

void MaterialDocumentSet::SetActive(std::size_t index)
{
    if (index < List.size())
        ActiveTab = index;
}

std::size_t MaterialDocumentSet::IndexOf(std::string_view virtualPath) const
{
    for (std::size_t i = 0; i < List.size(); ++i)
        if (List[i]->Session.VirtualPath() == virtualPath)
            return i;
    return List.size();
}

MaterialEditTab* MaterialDocumentSet::Find(std::string_view virtualPath)
{
    const std::size_t index = IndexOf(virtualPath);
    return index < List.size() ? List[index].get() : nullptr;
}

void MaterialDocumentSet::Renamed(MaterialEditTab& tab, std::string virtualPath, std::string filePath)
{
    tab.Commands.Clear();
    tab.Session.RenameTo(std::move(virtualPath), std::move(filePath));
}

void MaterialDocumentSet::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    for (const auto& tab : List)
        if (tab->Session.IsDirty())
            out.push_back(RefOf(*tab));
}

DocumentSaveResult MaterialDocumentSet::SaveDocument(std::string_view key)
{
    MaterialEditTab* tab = Find(key);
    if (tab == nullptr)
        return { {}, DocumentSaveStatus::Failed, "That material is not open." };
    if (tab->Session.IsExternallyModified())
        return { {}, DocumentSaveStatus::Conflict, {} };
    std::string error;
    if (!tab->Session.Save(&error))
        return { {}, DocumentSaveStatus::Failed, std::move(error) };
    return { {}, DocumentSaveStatus::Saved, {} };
}

bool MaterialDocumentSet::SettleDocument(std::string_view key, ConflictChoice choice, std::string& error)
{
    MaterialEditTab* tab = Find(key);
    if (tab == nullptr)
    {
        error = "That material is not open.";
        return false;
    }
    if (choice == ConflictChoice::KeepMine)
        return tab->Session.SaveOverFile(&error);
    // The file's version replaces the history the author's edits made.
    if (!tab->Session.ReloadFromFile(&error))
        return false;
    tab->Commands.Clear();
    return true;
}

void MaterialDocumentSet::StepDocument(std::string_view key, DocumentStep step)
{
    const std::size_t index = IndexOf(key);
    if (index >= List.size())
        return;
    step == DocumentStep::Undo ? List[index]->Commands.Undo() : List[index]->Commands.Redo();
    ActiveTab = index;
}

void MaterialDocumentSet::DiscardDocument(std::string_view key)
{
    std::string error;
    if (const std::size_t index = IndexOf(key); index < List.size())
        (void)Close(index, DirtyDisposition::Discard, error);
}

ExternalChange MaterialDocumentSet::FileChangedOnDisk(const std::filesystem::path& file)
{
    std::error_code ec;
    for (const auto& tab : List)
    {
        if (!std::filesystem::equivalent(tab->Session.FilePath(), file, ec))
            continue;
        if (!tab->Session.IsExternallyModified())
            return ExternalChange::Adopted; // its own write
        if (tab->Session.IsDirty())
            return ExternalChange::Held;
        std::string error;
        if (tab->Session.ReloadFromFile(&error))
            tab->Commands.Clear();
        return ExternalChange::Adopted;
    }
    return ExternalChange::NotOpen;
}
