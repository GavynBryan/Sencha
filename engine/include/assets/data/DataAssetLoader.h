#pragma once

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>
#include <core/metadata/DataSchema.h>

#include <memory>
#include <string>
#include <vector>

class AssetSystem;
class LoggingProvider;

struct CompiledDataAsset
{
    std::string TypeName;
    uint32_t Version = 0;
    std::shared_ptr<const void> Value;
};

// Stages a .sdata file: parse, check the envelope, validate `data` against the
// subtype's authoring schema, then hand it to the subtype's compiler. What the
// compiler returns is opaque here; only the subtype knows its own value type,
// which is what lets a game module add one without an engine edit.
class DataAssetLoader final : public IAssetStager
{
public:
    DataAssetLoader(LoggingProvider& logging,
                    DataAssetTypeRegistry* types,
                    DataSchemaRegistry* schemas,
                    DataAssetCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    // Loads the value's declared dependencies and has the entry hold them; one
    // that cannot load fails the commit.
    [[nodiscard]] DataAssetHandle CommitTyped(AssetStaging&& staged, AssetSystem& assets);
    [[nodiscard]] bool CommitReload(AssetStaging&& staged, AssetSystem& assets);

    // Refuses a value that declares dependencies.
    [[nodiscard]] DataAssetHandle CommitTyped(AssetStaging&& staged);
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    [[nodiscard]] bool LoadDependencies(const AssetStaging& staged, AssetSystem& assets,
                                        std::vector<AssetLease>& out);
    [[nodiscard]] DataAssetHandle Commit(AssetStaging&& staged, std::vector<AssetLease> dependencies);
    [[nodiscard]] bool Reload(AssetStaging&& staged, std::vector<AssetLease> dependencies);

    Logger& Log;
    DataAssetTypeRegistry* Types = nullptr;
    DataSchemaRegistry* Schemas = nullptr;
    DataAssetCache* Cache = nullptr;
    // Paths whose commit is loading its dependencies, outermost first.
    std::vector<std::string> Committing;
};
