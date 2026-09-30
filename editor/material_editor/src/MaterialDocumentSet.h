#pragma once

#include "MaterialEditSession.h"

#include "commands/CommandStack.h"
#include "documents/DocumentSource.h"

#include <core/assets/AssetLease.h>
#include <render/Material.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class DocumentSourceSet;

// One open material: its edit session, its own undo history, the lease on
// the resident preview material (taken and let go by the workspace), and the
// last session version pushed into the material cache.
struct MaterialEditTab
{
    MaterialEditSession Session;
    CommandStack Commands;
    AssetLease Resident;
    uint64_t AppliedVersion = 0;

    [[nodiscard]] MaterialHandle Handle() const
    {
        return MaterialHandle::FromToken(Resident.OpaqueToken());
    }
};

// The open materials, one tab each, as documents in the application's journal:
// a property edit is a step, a save refuses a file changed on disk since it
// was read, and closing a changed tab asks what to do with the change.
class MaterialDocumentSet final : public DocumentSource
{
public:
    // Pushes a description into a tab's resident material.
    using ResidentPush = std::function<void(MaterialEditTab& tab, const MaterialDescription& description)>;

    MaterialDocumentSet(DocumentSourceSet& sources, ResidentPush pushResident);
    ~MaterialDocumentSet() override;

    MaterialDocumentSet(const MaterialDocumentSet&) = delete;
    MaterialDocumentSet& operator=(const MaterialDocumentSet&) = delete;

    // Focuses the existing tab for virtualPath, or opens filePath in a new tab
    // (which becomes active). Null with *error set on parse failure.
    MaterialEditTab* OpenOrFocus(std::string virtualPath, std::string filePath, std::string* error);

    // Closes a tab. A changed one is refused, saved first, or dropped with its
    // resident material put back to the file's version, as `disposition` says.
    [[nodiscard]] bool Close(std::size_t index, DirtyDisposition disposition, std::string& error);

    [[nodiscard]] MaterialEditTab* Active();
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    void SetActive(std::size_t index);

    [[nodiscard]] MaterialEditTab* Find(std::string_view virtualPath);
    [[nodiscard]] const std::vector<std::unique_ptr<MaterialEditTab>>& Tabs() const { return List; }
    [[nodiscard]] DocumentRef RefOf(const MaterialEditTab& tab) { return { this, tab.Session.VirtualPath() }; }

    // The file moved on disk. Its history was keyed by the old path, so the
    // journal forgets it.
    void Renamed(MaterialEditTab& tab, std::string virtualPath, std::string filePath);

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override {}
    void DiscardDocument(std::string_view key) override;
    [[nodiscard]] ExternalChange FileChangedOnDisk(const std::filesystem::path& file) override;

private:
    [[nodiscard]] std::size_t IndexOf(std::string_view virtualPath) const;
    void Observe(MaterialEditTab& tab);

    DocumentSourceSet& Sources;
    ResidentPush PushResident;
    std::vector<std::unique_ptr<MaterialEditTab>> List;
    std::size_t ActiveTab = 0;
};
