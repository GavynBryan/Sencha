#pragma once

#include "data/DataDocument.h"
#include "data/DataResidentSync.h"
#include "documents/DocumentSource.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class DocumentSourceSet;
struct DataSchema;
struct RuntimeAssets;

// The application's open data documents, one per file however many views show
// it: their edits, saves, journal steps and resident assets. A view holds each
// document it shows; the last to let go closes it, and a document with changes
// is never dropped unless someone chose to.
class DataDocumentStore final : public DocumentSource
{
public:
    using ChangeObserver = std::function<void(DataDocument& document, bool residentChanged)>;
    using CloseObserver = std::function<void(const DataDocument& document)>;

    DataDocumentStore(RuntimeAssets& assets, DocumentSourceSet& sources);
    ~DataDocumentStore() override;

    DataDocumentStore(const DataDocumentStore&) = delete;
    DataDocumentStore& operator=(const DataDocumentStore&) = delete;

    // The document for a registered data asset, loaded on first request.
    DataDocument* Open(std::string_view virtualPath, std::string& error);
    // Writes the subtype's required members to a new file under `contentRoot`,
    // registers it, and opens it.
    DataDocument* Create(std::string_view subtype, const std::filesystem::path& contentRoot,
                         std::string_view relativePath, std::string& error);

    void Hold(const DataDocument& document);
    // Lets go of a held document. The last holder's release closes it: refused,
    // saved first, or dropped with its resident asset put back, as told.
    [[nodiscard]] bool Release(const DataDocument& document, DirtyDisposition disposition, std::string& error);

    [[nodiscard]] DataDocument* Find(std::string_view virtualPath);
    [[nodiscard]] const DataDocument* Find(std::string_view virtualPath) const;
    [[nodiscard]] DocumentRef RefOf(const DataDocument& document) { return { this, document.VirtualPath() }; }

    [[nodiscard]] const DataSchema* SchemaOf(const DataDocument& document) const;
    [[nodiscard]] const DataSchema* SchemaOf(std::string_view subtype) const;
    [[nodiscard]] const DataAssetTypeRegistry& Types() const;
    [[nodiscard]] bool IsRegistered(std::string_view virtualPath) const;
    // An open document's working subtype or root wins over the file's.
    [[nodiscard]] std::string SubtypeOf(std::string_view virtualPath) const;
    [[nodiscard]] std::optional<JsonValue> CurrentRoot(std::string_view virtualPath) const;
    [[nodiscard]] std::vector<std::string> DataAssetPaths(std::string_view subtype) const;

    void CommitEdit(DataDocument& document);
    void CancelEdit(DataDocument& document);
    void Validate(DataDocument& document);
    // Revalidates, keeps the resident asset on the committed version, and tells the observers.
    void Changed(DataDocument& document);
    [[nodiscard]] bool Reload(DataDocument& document, std::string& error);
    // True when a push that was waiting reached a resident asset.
    [[nodiscard]] bool PushWaiting() { return Resident.PushWaiting(); }
    [[nodiscard]] const DataResidentState* ResidentStateOf(const DataDocument& document) const
    {
        return Resident.StateOf(document);
    }

    // Tokens name a registration for Unobserve.
    std::size_t OnChanged(ChangeObserver observer);
    std::size_t OnClosing(CloseObserver observer);
    void Unobserve(std::size_t token);

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override;
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view key) override;
    [[nodiscard]] bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override;
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override;
    void DiscardDocument(std::string_view key) override;
    [[nodiscard]] ExternalChange FileChangedOnDisk(const std::filesystem::path& file) override;

private:
    struct Entry
    {
        std::unique_ptr<DataDocument> Document;
        std::size_t Holders = 0;
    };

    [[nodiscard]] std::optional<std::size_t> IndexOf(std::string_view virtualPath) const;
    [[nodiscard]] std::optional<std::size_t> IndexOf(const DataDocument& document) const;
    [[nodiscard]] bool CloseDocument(std::size_t index, DirtyDisposition disposition, std::string& error);
    DataDocument* Adopt(std::unique_ptr<DataDocument> document);

    RuntimeAssets& Assets;
    DocumentSourceSet& Sources;
    DataResidentSync Resident;
    std::vector<Entry> Open_;
    std::vector<std::pair<std::size_t, ChangeObserver>> ChangeObservers;
    std::vector<std::pair<std::size_t, CloseObserver>> CloseObservers;
    std::size_t NextToken = 1;
};
