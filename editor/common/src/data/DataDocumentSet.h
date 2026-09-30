#pragma once

#include "data/DataDocumentStore.h"
#include "ui/DataForm.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct DataDocumentSetConfig
{
    std::filesystem::path ContentRoot;
    // Empty opens every subtype.
    std::vector<std::string> Subtypes;
};

// One workspace's tabs over the application's data documents: which it shows,
// which is active, and the field selected in it. Documents themselves live in
// the store, so two workspaces showing one file edit one document.
class DataDocumentSet final : public DataFormHost
{
public:
    using ChangeObserver = DataDocumentStore::ChangeObserver;

    DataDocumentSet(DataDocumentStore& store, DataDocumentSetConfig config);
    ~DataDocumentSet() override;

    DataDocumentSet(const DataDocumentSet&) = delete;
    DataDocumentSet& operator=(const DataDocumentSet&) = delete;
    DataDocumentSet(DataDocumentSet&&) = delete;
    DataDocumentSet& operator=(DataDocumentSet&&) = delete;

    [[nodiscard]] DataDocumentStore& Store() { return Documents_; }
    [[nodiscard]] const DataDocumentStore& Store() const { return Documents_; }

    DataDocument* OpenOrFocus(std::string_view virtualPath, std::string& error);
    // Writes the subtype's required members to a new file under the content root, then opens it.
    DataDocument* Create(std::string_view subtype, std::string_view relativePath, std::string& error);
    // Drops the tab. The document closes with it when no other workspace shows
    // it, refused, saved or discarded as `disposition` says.
    [[nodiscard]] bool Close(std::size_t index, DirtyDisposition disposition, std::string& error);

    // Commits the active document's open edit before another becomes active.
    void SetActive(std::size_t index);
    // Brings an open document forward, as a journal step on it does.
    void Reveal(std::string_view virtualPath);
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    [[nodiscard]] DataDocument* Active();
    [[nodiscard]] DataDocument* ActiveOf(std::string_view subtype);
    [[nodiscard]] DataDocument* Find(std::string_view virtualPath);
    [[nodiscard]] const DataDocument* Find(std::string_view virtualPath) const;
    [[nodiscard]] std::optional<std::size_t> IndexOf(std::string_view virtualPath) const;
    [[nodiscard]] std::span<DataDocument* const> Documents() const { return Tabs; }
    [[nodiscard]] DocumentRef RefOf(const DataDocument& document) { return Documents_.RefOf(document); }
    [[nodiscard]] const DataSchema* ActiveSchema();
    // Subtypes this view opens that have both a registration and an authoring schema.
    [[nodiscard]] std::vector<std::string> CreatableSubtypes() const;

    // Told of every change to a document this view shows.
    void OnChanged(ChangeObserver observer) { Observer = std::move(observer); }

    void SelectField(const DataFieldSchema* field, std::string path);
    [[nodiscard]] const DataFieldSchema* SelectedField() const { return Selected; }
    [[nodiscard]] const std::string& SelectedPath() const { return SelectedJsonPath; }

    [[nodiscard]] std::vector<std::string> DataAssetPaths(std::string_view subtype) override;
    void OpenDataAsset(std::string_view path) override;
    void SelectField(const DataFieldSchema& field, std::string_view path) override;
    void EditPreviewed(DataDocument& document) override;
    void EditCommitted(DataDocument& document) override;

private:
    [[nodiscard]] bool Accepts(std::string_view subtype) const;
    DataDocument* Show(DataDocument& document);
    void Dropped(const DataDocument& document);

    DataDocumentStore& Documents_;
    DataDocumentSetConfig Config;
    std::vector<DataDocument*> Tabs;
    std::size_t ActiveTab = 0;
    const DataFieldSchema* Selected = nullptr;
    std::string SelectedJsonPath;
    ChangeObserver Observer;
    std::size_t ChangeToken = 0;
    std::size_t CloseToken = 0;
};
