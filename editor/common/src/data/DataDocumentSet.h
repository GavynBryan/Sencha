#pragma once

#include "data/DataDocumentStore.h"

#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
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

// How an edit lands in a document: within its open edit, opened if none is,
// or as a step of its own.
enum class DataEdit : std::uint8_t
{
    Preview,
    Step,
};

// One workspace's tabs over the application's data documents: which it shows
// and which is active. Documents themselves live in the store, so two
// workspaces showing one file edit one document.
class DataDocumentSet final
{
public:
    using ChangeObserver = DataDocumentStore::ChangeObserver;

    DataDocumentSet(DataDocumentStore& store, DataDocumentSetConfig config);
    ~DataDocumentSet();

    DataDocumentSet(const DataDocumentSet&) = delete;
    DataDocumentSet& operator=(const DataDocumentSet&) = delete;
    DataDocumentSet(DataDocumentSet&&) = delete;
    DataDocumentSet& operator=(DataDocumentSet&&) = delete;

    [[nodiscard]] DataDocumentStore& Store() { return Documents_; }
    [[nodiscard]] const DataDocumentStore& Store() const { return Documents_; }

    DataDocument* OpenOrFocus(std::string_view virtualPath, std::string& error);
    // Writes `data`, or the subtype's required members, to a new file under the
    // content root, then opens it.
    DataDocument* Create(std::string_view subtype, std::string_view relativePath, std::string& error,
                         const JsonValue* data = nullptr);
    // Drops the tab. The document closes with it when no other workspace shows
    // it, refused, saved or discarded as `disposition` says.
    [[nodiscard]] bool Close(std::size_t index, DirtyDisposition disposition, std::string& error);

    // Commits the active document's open edit before another becomes active.
    void SetActive(std::size_t index);
    // Brings an open document forward, as a journal step on it does.
    void Reveal(std::string_view virtualPath);
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    [[nodiscard]] DataDocument* Active();
    [[nodiscard]] const DataDocument* Active() const;
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

    // `root` as the document's new root, validated, and told to every view of
    // it once it is a step.
    void ApplyEdit(DataDocument& document, JsonValue root, DataEdit edit);

private:
    [[nodiscard]] bool Accepts(std::string_view subtype) const;
    DataDocument* Show(DataDocument& document);
    void Dropped(const DataDocument& document);

    DataDocumentStore& Documents_;
    DataDocumentSetConfig Config;
    std::vector<DataDocument*> Tabs;
    std::size_t ActiveTab = 0;
    ChangeObserver Observer;
    std::size_t ChangeToken = 0;
    std::size_t CloseToken = 0;
};
