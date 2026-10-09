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

// The data workspace's documents, and the data assets the project has.
class DataEditorWorkspace final
{
public:
    DataEditorWorkspace(RuntimeAssets& assets, const ProjectDescriptor& project, DocumentSourceSet& sources,
                        DataDocumentStore& store);

    DataEditorWorkspace(const DataEditorWorkspace&) = delete;
    DataEditorWorkspace& operator=(const DataEditorWorkspace&) = delete;
    DataEditorWorkspace(DataEditorWorkspace&&) = delete;
    DataEditorWorkspace& operator=(DataEditorWorkspace&&) = delete;

    [[nodiscard]] const DataAssetTypeRegistry& Types() const { return Assets.DataTypes; }
    [[nodiscard]] std::span<const DataAssetTypeRegistration> DataTypes() const { return Assets.DataTypes.Entries(); }
    [[nodiscard]] std::vector<const AssetRecord*> DataAssets() const;

private:
    RuntimeAssets& Assets;
    std::filesystem::path ContentRoot;

public:
    // The application's journal, which the store's documents are steps in.
    DocumentSourceSet& Sources;
    DataDocumentSet Documents;
};
