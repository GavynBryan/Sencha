#include "data/DataDocumentStore.h"

#include "data/DataAssetFiles.h"
#include "documents/DocumentSourceSet.h"

#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

DataDocumentStore::DataDocumentStore(RuntimeAssets& assets, DocumentSourceSet& sources)
    : Assets(assets)
    , Sources(sources)
    , Resident(assets)
{
    Sources.AddSource(*this);
}

DataDocumentStore::~DataDocumentStore()
{
    Sources.RemoveSource(*this);
}

DataDocument* DataDocumentStore::Open(std::string_view virtualPath, std::string& error)
{
    if (DataDocument* open = Find(virtualPath))
        return open;
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->Type != AssetType::Data)
    {
        error = std::format("'{}' is not a data asset registered in this project.", virtualPath);
        return nullptr;
    }
    std::unique_ptr<DataDocument> document =
        DataDocument::Open(record->FilePath, record->Path, Assets.DataTypes, Assets.DataSchemas, &error);
    return document != nullptr ? Adopt(std::move(document)) : nullptr;
}

DataDocument* DataDocumentStore::Create(std::string_view subtype, const std::filesystem::path& contentRoot,
                                        std::string_view relativePath, std::string& error)
{
    const DataAssetTypeRegistration* type = Assets.DataTypes.Find(subtype);
    const DataSchema* schema = Assets.DataSchemas.Find(subtype);
    if (type == nullptr || schema == nullptr)
    {
        error = std::format("'{}' is not a subtype that can be created.", subtype);
        return nullptr;
    }
    if (contentRoot.empty() || relativePath.empty())
    {
        error = "Name the new asset's path under the project's content root.";
        return nullptr;
    }
    const std::string relative = NormalizeDataAssetPath(relativePath);
    const std::filesystem::path file = contentRoot / relative;
    const std::string virtualPath = DataAssetVirtualPath(relative);
    if (DataAssetPathTaken(Assets.Registry, file, virtualPath))
    {
        error = std::format("'{}' already exists; choose another name.", relative);
        return nullptr;
    }
    std::unique_ptr<DataDocument> document = DataDocument::Create(file, virtualPath, *type, *schema);
    Validate(*document);
    if (!document->Save(&error))
        return nullptr;
    RegisterDataAssetFile(Assets.Registry, virtualPath, file);
    return Adopt(std::move(document));
}

void DataDocumentStore::Hold(const DataDocument& document)
{
    if (const std::optional<std::size_t> index = IndexOf(document))
        ++Open_[*index].Holders;
}

bool DataDocumentStore::Release(const DataDocument& document, DirtyDisposition disposition, std::string& error)
{
    const std::optional<std::size_t> index = IndexOf(document);
    if (!index)
        return true;
    Entry& entry = Open_[*index];
    if (entry.Holders > 1)
    {
        --entry.Holders;
        return true;
    }
    return CloseDocument(*index, disposition, error);
}

bool DataDocumentStore::CloseDocument(std::size_t index, DirtyDisposition disposition, std::string& error)
{
    DataDocument& document = *Open_[index].Document;
    const bool changed = document.IsDirty() || document.IsEditing();
    if (changed && disposition == DirtyDisposition::Refuse)
    {
        error = std::format("'{}' has unsaved changes.", document.VirtualPath());
        return false;
    }
    if (changed && disposition == DirtyDisposition::Save)
    {
        const DocumentSaveResult saved = Sources.Save(RefOf(document));
        if (saved.Status == DocumentSaveStatus::Conflict || saved.Status == DocumentSaveStatus::Failed)
        {
            error = saved.Status == DocumentSaveStatus::Conflict
                ? std::format("'{}' changed on disk since it was read; settle that first.", document.VirtualPath())
                : saved.Error;
            return false;
        }
    }
    document.CancelEdit();
    if (document.IsDirty())
        Resident.RestoreFromFile(document);
    else
        Resident.Forget(document);
    Sources.ForgetDocument(RefOf(document));
    // Every view drops its tab before the document goes.
    for (const auto& [token, observer] : CloseObservers)
        observer(document);
    Open_.erase(Open_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::optional<std::size_t> DataDocumentStore::IndexOf(std::string_view virtualPath) const
{
    for (std::size_t index = 0; index < Open_.size(); ++index)
        if (Open_[index].Document->VirtualPath() == virtualPath)
            return index;
    return std::nullopt;
}

std::optional<std::size_t> DataDocumentStore::IndexOf(const DataDocument& document) const
{
    for (std::size_t index = 0; index < Open_.size(); ++index)
        if (Open_[index].Document.get() == &document)
            return index;
    return std::nullopt;
}

DataDocument* DataDocumentStore::Find(std::string_view virtualPath)
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Open_[*index].Document.get() : nullptr;
}

const DataDocument* DataDocumentStore::Find(std::string_view virtualPath) const
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Open_[*index].Document.get() : nullptr;
}

const DataSchema* DataDocumentStore::SchemaOf(const DataDocument& document) const
{
    return Assets.DataSchemas.Find(document.Subtype());
}

const DataSchema* DataDocumentStore::SchemaOf(std::string_view subtype) const
{
    return Assets.DataSchemas.Find(subtype);
}

const DataAssetTypeRegistry& DataDocumentStore::Types() const
{
    return Assets.DataTypes;
}

bool DataDocumentStore::IsRegistered(std::string_view virtualPath) const
{
    return Assets.Registry.Contains(virtualPath);
}

std::string DataDocumentStore::SubtypeOf(std::string_view virtualPath) const
{
    if (const DataDocument* open = Find(virtualPath))
        return open->Subtype();
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    return record != nullptr && record->Type == AssetType::Data
        ? PeekDataAssetSubtype(Assets.Assets.DefaultSource(), *record)
        : std::string();
}

std::optional<JsonValue> DataDocumentStore::CurrentRoot(std::string_view virtualPath) const
{
    if (const DataDocument* open = Find(virtualPath))
        return open->Root();
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr)
        return std::nullopt;
    std::ifstream in(record->FilePath);
    std::stringstream text;
    text << in.rdbuf();
    return JsonParse(text.str());
}

std::vector<std::string> DataDocumentStore::DataAssetPaths(std::string_view subtype) const
{
    std::vector<std::string> paths;
    for (const auto& [path, record] : Assets.Registry.Records())
        if (record.Type == AssetType::Data && (subtype.empty() || SubtypeOf(path) == subtype))
            paths.push_back(path);
    std::ranges::sort(paths);
    return paths;
}

void DataDocumentStore::CommitEdit(DataDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CommitEdit();
    Changed(document);
}

void DataDocumentStore::CancelEdit(DataDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CancelEdit();
    Changed(document);
}

void DataDocumentStore::Validate(DataDocument& document)
{
    document.Validate(Assets.DataTypes, Assets.DataSchemas);
}

void DataDocumentStore::Changed(DataDocument& document)
{
    Validate(document);
    const bool residentChanged = Resident.Push(document);
    for (const auto& [token, observer] : ChangeObservers)
        observer(document, residentChanged);
}

bool DataDocumentStore::Reload(DataDocument& document, std::string& error)
{
    CancelEdit(document);
    if (document.IsDirty())
    {
        error = "Reload refused: undo or save local edits first.";
        return false;
    }
    if (!document.Reload(Assets.DataTypes, Assets.DataSchemas, &error))
        return false;
    Sources.ForgetDocument(RefOf(document));
    Resident.Forget(document);
    Changed(document);
    return true;
}

std::size_t DataDocumentStore::OnChanged(ChangeObserver observer)
{
    ChangeObservers.emplace_back(NextToken, std::move(observer));
    return NextToken++;
}

std::size_t DataDocumentStore::OnClosing(CloseObserver observer)
{
    CloseObservers.emplace_back(NextToken, std::move(observer));
    return NextToken++;
}

void DataDocumentStore::Unobserve(std::size_t token)
{
    std::erase_if(ChangeObservers, [token](const auto& entry) { return entry.first == token; });
    std::erase_if(CloseObservers, [token](const auto& entry) { return entry.first == token; });
}

void DataDocumentStore::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    for (const Entry& entry : Open_)
        if (entry.Document->IsDirty() || entry.Document->IsEditing())
            out.push_back(RefOf(*entry.Document));
}

DocumentSaveResult DataDocumentStore::SaveDocument(std::string_view key)
{
    DataDocument* document = Find(key);
    if (document == nullptr)
        return { {}, DocumentSaveStatus::Failed, "That document is not open." };
    const bool wasEditing = document->IsEditing();
    std::string error;
    const bool saved = document->Save(&error);
    if (wasEditing)
        Changed(*document);
    if (!saved)
        return { {}, document->IsExternallyModified() ? DocumentSaveStatus::Conflict : DocumentSaveStatus::Failed,
                 std::move(error) };
    RegisterDataAssetFile(Assets.Registry, document->VirtualPath(), document->FilePath());
    return { {}, document->IsSemanticallyValid() ? DocumentSaveStatus::Saved : DocumentSaveStatus::SavedWithProblems,
             {} };
}

bool DataDocumentStore::SettleDocument(std::string_view key, ConflictChoice choice, std::string& error)
{
    DataDocument* document = Find(key);
    if (document == nullptr)
    {
        error = "That document is not open.";
        return false;
    }
    if (choice == ConflictChoice::KeepMine)
        return document->SaveOverFile(&error);
    if (!document->AdoptFileVersion(Assets.DataTypes, Assets.DataSchemas, &error))
        return false;
    Changed(*document);
    return true;
}

void DataDocumentStore::StepDocument(std::string_view key, DocumentStep step)
{
    DataDocument* document = Find(key);
    if (document == nullptr)
        return;
    step == DocumentStep::Undo ? document->Undo() : document->Redo();
    Changed(*document);
}

void DataDocumentStore::CancelDocumentEdits()
{
    for (const Entry& entry : Open_)
        CancelEdit(*entry.Document);
}

void DataDocumentStore::DiscardDocument(std::string_view key)
{
    std::string error;
    if (const std::optional<std::size_t> index = IndexOf(key))
        (void)CloseDocument(*index, DirtyDisposition::Discard, error);
}

ExternalChange DataDocumentStore::FileChangedOnDisk(const std::filesystem::path& file)
{
    std::error_code ec;
    for (const Entry& entry : Open_)
    {
        DataDocument& document = *entry.Document;
        if (!std::filesystem::equivalent(document.FilePath(), file, ec))
            continue;
        if (!document.IsExternallyModified())
            return ExternalChange::Adopted; // its own write
        if (document.IsDirty() || document.IsEditing())
            return ExternalChange::Held;
        // Taken as one step, so the version before it is an undo away.
        std::string error;
        if (document.AdoptFileVersion(Assets.DataTypes, Assets.DataSchemas, &error))
            Changed(document);
        return ExternalChange::Adopted;
    }
    return ExternalChange::NotOpen;
}

DataDocument* DataDocumentStore::Adopt(std::unique_ptr<DataDocument> document)
{
    document->ObserveSteps([this, key = document->VirtualPath()] { Sources.Record({ this, key }); });
    Open_.push_back(Entry{ std::move(document), 0 });
    return Open_.back().Document.get();
}
