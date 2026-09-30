#pragma once

#include "data/DataDocumentSet.h"
#include "documents/DocumentSourceSet.h"

#include <assets/runtime/RuntimeAssets.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ProjectDescriptor;

// The data workspace's documents, plus the file operations only it offers. Each
// operation that would drop an open document's changes refuses unless told.
class DataEditorWorkspace final
{
public:
    DataEditorWorkspace(RuntimeAssets& assets, const ProjectDescriptor& project, DocumentSourceSet& sources,
                        DataDocumentStore& store);

    DataEditorWorkspace(const DataEditorWorkspace&) = delete;
    DataEditorWorkspace& operator=(const DataEditorWorkspace&) = delete;
    DataEditorWorkspace(DataEditorWorkspace&&) = delete;
    DataEditorWorkspace& operator=(DataEditorWorkspace&&) = delete;

    [[nodiscard]] bool Duplicate(std::string_view virtualPath, std::string_view relativePath, std::string& error);
    [[nodiscard]] bool Rename(std::string_view virtualPath, std::string_view relativePath,
                              DirtyDisposition disposition, std::string& error);
    [[nodiscard]] bool Delete(std::string_view virtualPath, DirtyDisposition disposition, std::string& error);

    [[nodiscard]] const DataAssetTypeRegistry& Types() const { return Assets.DataTypes; }
    [[nodiscard]] std::span<const DataAssetTypeRegistration> DataTypes() const { return Assets.DataTypes.Entries(); }
    [[nodiscard]] std::vector<const AssetRecord*> DataAssets() const;

private:
    [[nodiscard]] bool CloseIfOpen(std::string_view virtualPath, DirtyDisposition disposition, std::string& error);

    RuntimeAssets& Assets;
    std::filesystem::path ContentRoot;

public:
    // The application's journal, which the store's documents are steps in.
    DocumentSourceSet& Sources;
    DataDocumentSet Documents;
};
