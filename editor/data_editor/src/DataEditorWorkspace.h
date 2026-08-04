#pragma once

#include "DataDocument.h"

#include <assets/runtime/RuntimeAssets.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ProjectDescriptor;

// What the last save did, so the editor can say whether a running game will
// pick the change up. The editor cannot observe the game process, so a valid
// save reports what the file now permits, not a confirmed reload.
struct DataSaveReport
{
    std::string VirtualPath;
    bool SemanticallyValid = false;
    bool Saved = false;
};

class DataEditorWorkspace
{
public:
    DataEditorWorkspace(RuntimeAssets& assets, const ProjectDescriptor& project);

    [[nodiscard]] bool Open(std::string_view virtualPath, std::string* error = nullptr);
    [[nodiscard]] bool Create(std::string_view subtype,
                              std::string_view relativePath,
                              std::string* error = nullptr);
    [[nodiscard]] bool Duplicate(std::string_view virtualPath,
                                 std::string_view relativePath,
                                 std::string* error = nullptr);
    [[nodiscard]] bool Rename(std::string_view virtualPath,
                              std::string_view relativePath,
                              std::string* error = nullptr);
    [[nodiscard]] bool Delete(std::string_view virtualPath,
                              std::string* error = nullptr);

    void Close(std::size_t index);
    void SetActive(std::size_t index);

    [[nodiscard]] DataDocument* Active();
    [[nodiscard]] const DataDocument* Active() const;
    [[nodiscard]] std::size_t ActiveIndex() const { return ActiveTab; }
    [[nodiscard]] std::vector<std::unique_ptr<DataDocument>>& Documents() { return Tabs; }
    [[nodiscard]] const std::vector<std::unique_ptr<DataDocument>>& Documents() const { return Tabs; }

    [[nodiscard]] bool SaveActive(std::string* error = nullptr);
    void SaveAll();
    void ValidateActive();
    [[nodiscard]] const DataSaveReport& LastSaveReport() const { return LastSave; }

    [[nodiscard]] const DataSchema* ActiveSchema() const;
    [[nodiscard]] const DataAssetTypeRegistry& Types() const { return Assets.DataTypes; }
    [[nodiscard]] std::span<const DataAssetTypeRegistration> DataTypes() const
    {
        return Assets.DataTypes.Entries();
    }
    void SelectField(const DataFieldSchema* field, std::string path);
    [[nodiscard]] const DataFieldSchema* SelectedField() const { return Selected; }
    [[nodiscard]] const std::string& SelectedPath() const { return SelectedJsonPath; }

    [[nodiscard]] std::vector<const AssetRecord*> DataAssets() const;
    [[nodiscard]] std::filesystem::path ResolveFile(std::string_view virtualPath) const;
    [[nodiscard]] std::string MakeVirtualPath(std::string_view relativePath) const;

private:
    void RegisterFile(std::string_view virtualPath, const std::filesystem::path& file);
    void ReloadResident(std::string_view virtualPath);

    RuntimeAssets& Assets;
    const ProjectDescriptor& Project;
    std::vector<std::unique_ptr<DataDocument>> Tabs;
    std::size_t ActiveTab = 0;
    const DataFieldSchema* Selected = nullptr;
    std::string SelectedJsonPath;
    DataSaveReport LastSave;
};
