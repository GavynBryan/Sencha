#include "data/DataDocumentSet.h"

#include "data/DataAssetFiles.h"

#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

DataDocumentSet::DataDocumentSet(RuntimeAssets& assets, DocumentSourceSet& sources, DataDocumentSetConfig config)
    : Assets(assets)
    , Sources(sources)
    , Config(std::move(config))
    , Resident(assets)
{
    Sources.AddSource(*this);
}

DataDocumentSet::~DataDocumentSet()
{
    Sources.RemoveSource(*this);
}

DataDocument* DataDocumentSet::OpenOrFocus(std::string_view virtualPath, std::string& error)
{
    if (const std::optional<std::size_t> index = IndexOf(virtualPath))
    {
        SetActive(*index);
        return Open[*index].get();
    }
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->Type != AssetType::Data)
    {
        error = std::format("'{}' is not a data asset registered in this project.", virtualPath);
        return nullptr;
    }
    std::unique_ptr<DataDocument> document =
        DataDocument::Open(record->FilePath, record->Path, Assets.DataTypes, Assets.DataSchemas, &error);
    if (document == nullptr)
        return nullptr;
    if (!Accepts(document->Subtype()))
    {
        error = std::format("'{}' is a {} asset, which this editor does not open.", virtualPath, document->Subtype());
        return nullptr;
    }
    return Adopt(std::move(document));
}

DataDocument* DataDocumentSet::Create(std::string_view subtype, std::string_view relativePath, std::string& error)
{
    const DataAssetTypeRegistration* type = Assets.DataTypes.Find(subtype);
    const DataSchema* schema = Assets.DataSchemas.Find(subtype);
    if (type == nullptr || schema == nullptr || !Accepts(subtype))
    {
        error = std::format("'{}' is not a subtype this editor can create.", subtype);
        return nullptr;
    }
    if (Config.ContentRoot.empty() || relativePath.empty())
    {
        error = "Name the new asset's path under the project's content root.";
        return nullptr;
    }
    const std::string relative = NormalizeDataAssetPath(relativePath);
    const std::filesystem::path file = Config.ContentRoot / relative;
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

bool DataDocumentSet::Close(std::size_t index, DirtyDisposition disposition, std::string& error)
{
    if (index >= Open.size())
    {
        error = "That document is not open.";
        return false;
    }
    DataDocument& document = *Open[index];
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
    Open.erase(Open.begin() + static_cast<std::ptrdiff_t>(index));
    if (ActiveTab > index || ActiveTab >= Open.size())
        ActiveTab = ActiveTab == 0 ? 0 : ActiveTab - 1;
    SelectField(nullptr, {});
    return true;
}

void DataDocumentSet::SetActive(std::size_t index)
{
    if (index >= Open.size())
        return;
    if (index != ActiveTab)
        if (DataDocument* previous = Active())
            CommitEdit(*previous);
    ActiveTab = index;
    SelectField(nullptr, {});
}

DataDocument* DataDocumentSet::Active()
{
    return ActiveTab < Open.size() ? Open[ActiveTab].get() : nullptr;
}

DataDocument* DataDocumentSet::ActiveOf(std::string_view subtype)
{
    DataDocument* active = Active();
    return active != nullptr && active->Subtype() == subtype ? active : nullptr;
}

DataDocument* DataDocumentSet::Find(std::string_view virtualPath)
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Open[*index].get() : nullptr;
}

const DataDocument* DataDocumentSet::Find(std::string_view virtualPath) const
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Open[*index].get() : nullptr;
}

std::optional<std::size_t> DataDocumentSet::IndexOf(std::string_view virtualPath) const
{
    for (std::size_t index = 0; index < Open.size(); ++index)
        if (Open[index]->VirtualPath() == virtualPath)
            return index;
    return std::nullopt;
}

const DataSchema* DataDocumentSet::SchemaOf(const DataDocument& document) const
{
    return Assets.DataSchemas.Find(document.Subtype());
}

void DataDocumentSet::CommitEdit(DataDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CommitEdit();
    Changed(document);
}

void DataDocumentSet::CancelEdit(DataDocument& document)
{
    if (!document.IsEditing())
        return;
    document.CancelEdit();
    Changed(document);
}

void DataDocumentSet::Changed(DataDocument& document)
{
    Validate(document);
    const bool residentChanged = Resident.Push(document);
    if (Observer)
        Observer(document, residentChanged);
}

bool DataDocumentSet::Reload(DataDocument& document, std::string& error)
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

std::string DataDocumentSet::SubtypeOf(std::string_view virtualPath) const
{
    if (const DataDocument* open = Find(virtualPath))
        return open->Subtype();
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    return record != nullptr && record->Type == AssetType::Data
        ? PeekDataAssetSubtype(Assets.Assets.DefaultSource(), *record)
        : std::string();
}

std::optional<JsonValue> DataDocumentSet::CurrentRoot(std::string_view virtualPath) const
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

void DataDocumentSet::SelectField(const DataFieldSchema* field, std::string path)
{
    Selected = field;
    SelectedJsonPath = std::move(path);
}

std::vector<std::string> DataDocumentSet::DataAssetPaths(std::string_view subtype)
{
    std::vector<std::string> paths;
    for (const auto& [path, record] : Assets.Registry.Records())
        if (record.Type == AssetType::Data && (subtype.empty() || SubtypeOf(path) == subtype))
            paths.push_back(path);
    std::ranges::sort(paths);
    return paths;
}

void DataDocumentSet::OpenDataAsset(std::string_view path)
{
    std::string error;
    (void)OpenOrFocus(path, error);
}

void DataDocumentSet::SelectField(const DataFieldSchema& field, std::string_view path)
{
    SelectField(&field, std::string(path));
}

void DataDocumentSet::EditPreviewed(DataDocument& document)
{
    Validate(document);
}

void DataDocumentSet::EditCommitted(DataDocument& document)
{
    Changed(document);
}

void DataDocumentSet::AppendChangedDocuments(std::vector<DocumentRef>& out)
{
    for (const auto& document : Open)
        if (document->IsDirty() || document->IsEditing())
            out.push_back(RefOf(*document));
}

DocumentSaveResult DataDocumentSet::SaveDocument(std::string_view key)
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

bool DataDocumentSet::SettleDocument(std::string_view key, ConflictChoice choice, std::string& error)
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

void DataDocumentSet::StepDocument(std::string_view key, DocumentStep step)
{
    const std::optional<std::size_t> index = IndexOf(key);
    if (!index)
        return;
    DataDocument& document = *Open[*index];
    step == DocumentStep::Undo ? document.Undo() : document.Redo();
    Changed(document);
    SetActive(*index);
}

void DataDocumentSet::CancelDocumentEdits()
{
    for (const auto& document : Open)
        CancelEdit(*document);
}

bool DataDocumentSet::Accepts(std::string_view subtype) const
{
    return Config.Subtypes.empty() || std::ranges::find(Config.Subtypes, subtype) != Config.Subtypes.end();
}

DataDocument* DataDocumentSet::Adopt(std::unique_ptr<DataDocument> document)
{
    document->ObserveSteps([this, key = document->VirtualPath()] { Sources.Record({ this, key }); });
    Open.push_back(std::move(document));
    SetActive(Open.size() - 1);
    return Open.back().get();
}

void DataDocumentSet::Validate(DataDocument& document)
{
    document.Validate(Assets.DataTypes, Assets.DataSchemas);
}
