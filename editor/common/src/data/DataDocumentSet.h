#pragma once

#include "data/DataDocument.h"
#include "data/DataResidentSync.h"
#include "documents/DocumentSource.h"
#include "documents/DocumentSourceSet.h"
#include "ui/DataForm.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct RuntimeAssets;

struct DataDocumentSetConfig
{
    std::filesystem::path ContentRoot;
    // Empty opens every subtype.
    std::vector<std::string> Subtypes;
};

// The open data documents of one editor: their edits, saves, journal steps and
// resident assets. A document with changes is never dropped unless the caller chose to.
class DataDocumentSet final : public DataFormHost, public DocumentSource
{
public:
    using ChangeObserver = std::function<void(DataDocument& document, bool residentChanged)>;

    DataDocumentSet(RuntimeAssets& assets, DocumentSourceSet& sources, DataDocumentSetConfig config);
    ~DataDocumentSet() override;

    DataDocumentSet(const DataDocumentSet&) = delete;
    DataDocumentSet& operator=(const DataDocumentSet&) = delete;
    DataDocumentSet(DataDocumentSet&&) = delete;
    DataDocumentSet& operator=(DataDocumentSet&&) = delete;

    DataDocument* OpenOrFocus(std::string_view virtualPath, std::string& error);
    // Writes the subtype's required members to a new file under the content root, then opens it.
    DataDocument* Create(std::string_view subtype, std::string_view relativePath, std::string& error);
    [[nodiscard]] bool Close(std::size_t index, DirtyDisposition disposition, std::string& error);

    // Commits the active document's open edit before another becomes active.
    void SetActive(std::size_t index);
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    [[nodiscard]] DataDocument* Active();
    [[nodiscard]] DataDocument* ActiveOf(std::string_view subtype);
    [[nodiscard]] DataDocument* Find(std::string_view virtualPath);
    [[nodiscard]] const DataDocument* Find(std::string_view virtualPath) const;
    [[nodiscard]] std::optional<std::size_t> IndexOf(std::string_view virtualPath) const;
    [[nodiscard]] std::span<const std::unique_ptr<DataDocument>> Documents() const { return Open; }
    [[nodiscard]] DocumentRef RefOf(const DataDocument& document) { return { this, document.VirtualPath() }; }
    [[nodiscard]] const DataSchema* SchemaOf(const DataDocument& document) const;
    [[nodiscard]] const DataSchema* ActiveSchema();
    [[nodiscard]] const DataAssetTypeRegistry& Types() const;
    // Subtypes this set opens that have both a registration and an authoring schema.
    [[nodiscard]] std::vector<std::string> CreatableSubtypes() const;

    void CommitEdit(DataDocument& document);
    void CancelEdit(DataDocument& document);
    // Revalidates, keeps the resident asset on the committed version, and tells the observer.
    void Changed(DataDocument& document);
    [[nodiscard]] bool Reload(DataDocument& document, std::string& error);
    void OnChanged(ChangeObserver observer) { Observer = std::move(observer); }
    // True when a push that was waiting reached a resident asset.
    [[nodiscard]] bool PushWaiting() { return Resident.PushWaiting(); }
    [[nodiscard]] const DataResidentState* ResidentStateOf(const DataDocument& document) const
    {
        return Resident.StateOf(document);
    }

    // An open document's working subtype or root wins over the file's.
    [[nodiscard]] std::string SubtypeOf(std::string_view virtualPath) const;
    [[nodiscard]] std::optional<JsonValue> CurrentRoot(std::string_view virtualPath) const;

    void SelectField(const DataFieldSchema* field, std::string path);
    [[nodiscard]] const DataFieldSchema* SelectedField() const { return Selected; }
    [[nodiscard]] const std::string& SelectedPath() const { return SelectedJsonPath; }

    [[nodiscard]] std::vector<std::string> DataAssetPaths(std::string_view subtype) override;
    void OpenDataAsset(std::string_view path) override;
    void SelectField(const DataFieldSchema& field, std::string_view path) override;
    void EditPreviewed(DataDocument& document) override;
    void EditCommitted(DataDocument& document) override;

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override;

private:
    [[nodiscard]] bool Accepts(std::string_view subtype) const;
    DataDocument* Adopt(std::unique_ptr<DataDocument> document);
    void Validate(DataDocument& document);

    RuntimeAssets& Assets;
    DocumentSourceSet& Sources;
    DataDocumentSetConfig Config;
    DataResidentSync Resident;
    std::vector<std::unique_ptr<DataDocument>> Open;
    std::size_t ActiveTab = 0;
    const DataFieldSchema* Selected = nullptr;
    std::string SelectedJsonPath;
    ChangeObserver Observer;
};
