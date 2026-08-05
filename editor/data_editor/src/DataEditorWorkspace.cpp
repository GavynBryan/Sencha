#include "DataEditorWorkspace.h"

#include "project/Project.h"

#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace
{
    std::string NormalizeRelativeDataPath(std::string_view path)
    {
        std::filesystem::path relative{std::string(path)};
        if (relative.extension() != ".sdata")
            relative += ".sdata";
        return relative.lexically_normal().generic_string();
    }
}

DataEditorWorkspace::DataEditorWorkspace(RuntimeAssets& assets,
                                         const ProjectDescriptor& project)
    : Assets(assets)
    , Project(project)
{
}

bool DataEditorWorkspace::Open(std::string_view virtualPath, std::string* error)
{
    for (std::size_t index = 0; index < Tabs.size(); ++index)
    {
        if (Tabs[index]->VirtualPath() == virtualPath)
        {
            SetActive(index);
            return true;
        }
    }

    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->Type != AssetType::Data)
    {
        if (error)
            *error = "data asset is not registered";
        return false;
    }

    std::unique_ptr<DataDocument> document = DataDocument::Open(
        record->FilePath, record->Path, Assets.DataTypes, Assets.DataSchemas, error);
    if (!document)
        return false;

    Tabs.push_back(std::move(document));
    SetActive(Tabs.size() - 1);
    return true;
}

bool DataEditorWorkspace::Create(std::string_view subtype,
                                 std::string_view relativePath,
                                 std::string* error)
{
    if (Project.ContentRoots.empty())
    {
        if (error)
            *error = "project has no content roots";
        return false;
    }

    const DataAssetTypeRegistration* type = Assets.DataTypes.Find(subtype);
    const DataSchema* schema = Assets.DataSchemas.Find(subtype);
    if (type == nullptr || schema == nullptr)
    {
        if (error)
            *error = "data subtype has no runtime registration or authoring schema";
        return false;
    }

    const std::string relative = NormalizeRelativeDataPath(relativePath);
    const std::filesystem::path file =
        std::filesystem::path(Project.ContentRoots.front()) / relative;
    const std::string virtualPath = MakeVirtualPath(relative);
    if (std::filesystem::exists(file) || Assets.Registry.Contains(virtualPath))
    {
        if (error)
            *error = "a data asset already exists at that path";
        return false;
    }

    std::unique_ptr<DataDocument> document =
        DataDocument::Create(file, virtualPath, *type, *schema);
    document->Validate(Assets.DataTypes, Assets.DataSchemas);
    if (!document->Save(error))
        return false;

    RegisterFile(virtualPath, file);
    Tabs.push_back(std::move(document));
    SetActive(Tabs.size() - 1);
    return true;
}

bool DataEditorWorkspace::Duplicate(std::string_view virtualPath,
                                    std::string_view relativePath,
                                    std::string* error)
{
    const AssetRecord* source = Assets.Registry.FindByPath(virtualPath);
    if (source == nullptr || source->Type != AssetType::Data || Project.ContentRoots.empty())
    {
        if (error)
            *error = "source data asset is not registered";
        return false;
    }

    const std::string relative = NormalizeRelativeDataPath(relativePath);
    const std::filesystem::path destination =
        std::filesystem::path(Project.ContentRoots.front()) / relative;
    const std::string destinationVirtual = MakeVirtualPath(relative);
    if (std::filesystem::exists(destination) || Assets.Registry.Contains(destinationVirtual))
    {
        if (error)
            *error = "destination already exists";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }
    std::filesystem::copy_file(source->FilePath, destination,
                               std::filesystem::copy_options::none, ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }

    RegisterFile(destinationVirtual, destination);
    return Open(destinationVirtual, error);
}

bool DataEditorWorkspace::Rename(std::string_view virtualPath,
                                 std::string_view relativePath,
                                 std::string* error)
{
    const AssetRecord* source = Assets.Registry.FindByPath(virtualPath);
    if (source == nullptr || source->Type != AssetType::Data || Project.ContentRoots.empty())
    {
        if (error)
            *error = "source data asset is not registered";
        return false;
    }

    const AssetRecord oldRecord = *source;
    const std::string relative = NormalizeRelativeDataPath(relativePath);
    const std::filesystem::path destination =
        std::filesystem::path(Project.ContentRoots.front()) / relative;
    const std::string destinationVirtual = MakeVirtualPath(relative);
    if (std::filesystem::exists(destination) || Assets.Registry.Contains(destinationVirtual))
    {
        if (error)
            *error = "destination already exists";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (!ec)
        std::filesystem::rename(oldRecord.FilePath, destination, ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }

    for (std::size_t index = Tabs.size(); index-- > 0;)
    {
        if (Tabs[index]->VirtualPath() == virtualPath)
            Close(index);
    }

    (void)Assets.Registry.Unregister(virtualPath);
    RegisterFile(destinationVirtual, destination);
    return Open(destinationVirtual, error);
}

bool DataEditorWorkspace::Delete(std::string_view virtualPath, std::string* error)
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->Type != AssetType::Data)
    {
        if (error)
            *error = "data asset is not registered";
        return false;
    }

    const std::filesystem::path file = record->FilePath;
    std::error_code ec;
    std::filesystem::remove(file, ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }

    for (std::size_t index = Tabs.size(); index-- > 0;)
    {
        if (Tabs[index]->VirtualPath() == virtualPath)
            Close(index);
    }
    (void)Assets.Registry.Unregister(virtualPath);
    return true;
}

void DataEditorWorkspace::Close(std::size_t index)
{
    if (index >= Tabs.size())
        return;

    // An interaction still in flight would otherwise be destroyed mid-transaction.
    Tabs[index]->CancelEdit();
    Tabs.erase(Tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (Tabs.empty())
        ActiveTab = 0;
    else if (ActiveTab >= Tabs.size())
        ActiveTab = Tabs.size() - 1;
    Selected = nullptr;
    SelectedJsonPath.clear();
}

void DataEditorWorkspace::SetActive(std::size_t index)
{
    if (index >= Tabs.size())
        return;
    // Leaving a document mid-drag keeps what the pointer already produced;
    // abandoning it silently would lose an edit the author watched happen.
    if (index != ActiveTab)
    {
        if (DataDocument* previous = Active())
            previous->CommitEdit();
    }
    ActiveTab = index;
    Selected = nullptr;
    SelectedJsonPath.clear();
}

DataDocument* DataEditorWorkspace::Active()
{
    return ActiveTab < Tabs.size() ? Tabs[ActiveTab].get() : nullptr;
}

const DataDocument* DataEditorWorkspace::Active() const
{
    return ActiveTab < Tabs.size() ? Tabs[ActiveTab].get() : nullptr;
}

bool DataEditorWorkspace::SaveActive(std::string* error)
{
    DataDocument* document = Active();
    if (document == nullptr)
        return false;

    // Saving mid-drag writes what is on screen, so the interaction has to land
    // on the undo stack rather than stay pending behind the written file.
    document->CommitEdit();

    document->Validate(Assets.DataTypes, Assets.DataSchemas);
    if (!document->Save(error))
        return false;

    RegisterFile(document->VirtualPath(), document->FilePath());
    const bool valid = document->IsSemanticallyValid();
    if (valid)
        ReloadResident(document->VirtualPath());

    LastSave = DataSaveReport{
        .VirtualPath = document->VirtualPath(),
        .SemanticallyValid = valid,
        .Saved = true,
    };
    return true;
}

void DataEditorWorkspace::SaveAll()
{
    const std::size_t previous = ActiveTab;
    for (std::size_t index = 0; index < Tabs.size(); ++index)
    {
        ActiveTab = index;
        std::string ignored;
        (void)SaveActive(&ignored);
    }
    if (!Tabs.empty())
        ActiveTab = std::min(previous, Tabs.size() - 1);
}

void DataEditorWorkspace::ValidateActive()
{
    if (DataDocument* document = Active())
        document->Validate(Assets.DataTypes, Assets.DataSchemas);
}

const DataSchema* DataEditorWorkspace::ActiveSchema() const
{
    const DataDocument* document = Active();
    return document ? Assets.DataSchemas.Find(document->Subtype()) : nullptr;
}

void DataEditorWorkspace::SelectField(const DataFieldSchema* field, std::string path)
{
    Selected = field;
    SelectedJsonPath = std::move(path);
}

std::vector<const AssetRecord*> DataEditorWorkspace::DataAssets() const
{
    std::vector<const AssetRecord*> records;
    for (const auto& [path, record] : Assets.Registry.Records())
    {
        (void)path;
        if (record.Type == AssetType::Data)
            records.push_back(&record);
    }
    std::sort(records.begin(), records.end(),
        [](const AssetRecord* a, const AssetRecord* b)
        {
            return a->Path < b->Path;
        });
    return records;
}

std::string DataEditorWorkspace::DataSubtypeOf(std::string_view virtualPath) const
{
    // An open tab's live envelope wins over the file: a subtype-filtered picker
    // that read only disk would miss an asset the author just created and has
    // not saved.
    for (const auto& tab : Tabs)
    {
        if (tab->VirtualPath() == virtualPath)
            return std::string(tab->Subtype());
    }

    const std::filesystem::path file = ResolveFile(virtualPath);
    if (file.empty())
        return {};

    std::ifstream stream(file);
    if (!stream)
        return {};
    std::ostringstream contents;
    contents << stream.rdbuf();

    const std::optional<JsonValue> root = JsonParse(contents.str());
    if (!root.has_value())
        return {};
    const JsonValue* type = root->Find("type");
    return type != nullptr && type->IsString() ? type->AsString() : std::string{};
}

std::filesystem::path DataEditorWorkspace::ResolveFile(std::string_view virtualPath) const
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    return record ? std::filesystem::path(record->FilePath) : std::filesystem::path{};
}

std::string DataEditorWorkspace::MakeVirtualPath(std::string_view relativePath) const
{
    return std::string("asset://") + NormalizeRelativeDataPath(relativePath);
}

void DataEditorWorkspace::RegisterFile(std::string_view virtualPath,
                                       const std::filesystem::path& file)
{
    AssetRecord record;
    record.Type = AssetType::Data;
    record.SourceKind = AssetSourceKind::File;
    record.Path = std::string(virtualPath);
    record.FilePath = file.generic_string();
    (void)Assets.Registry.RegisterOrVerify(record);
}

void DataEditorWorkspace::ReloadResident(std::string_view virtualPath)
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || !Assets.Assets.IsResident(virtualPath, AssetType::Data))
        return;

    AssetStaging staged = Assets.DataLoader.LoadStaged(*record, Assets.Assets.DefaultSource());
    if (staged.IsValid())
        (void)Assets.DataLoader.CommitReload(std::move(staged));
}
