#pragma once

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/data/DataDefinitionStaging.h>
#include <core/assets/AssetCommitResult.h>
#include <core/assets/AssetStager.h>
#include <core/json/JsonValue.h>
#include <core/logging/Logger.h>
#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssetSystem;
class LoggingProvider;

struct CompiledDataAsset
{
    std::string TypeName;
    uint32_t Version = 0;
    std::shared_ptr<const void> Value;
    std::vector<std::string> Sources;
};

// The compiled value is opaque here, so a game module adds a subtype without an engine edit.
// A definition with sources composes when it commits, on the owner thread, unless given a reader.
class DataAssetLoader final : public IAssetStager
{
public:
    DataAssetLoader(LoggingProvider& logging,
                    DataAssetTypeRegistry* types,
                    DataSchemaRegistry* schemas,
                    DataAssetCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source,
                                          const AssetPathsById& current) override;
    // Stages an envelope already in memory, such as an editor's working copy.
    [[nodiscard]] AssetStaging StageRoot(const AssetRecord& record, const JsonValue& root,
                                         const AssetPathsById& current, const DataSourceReader& read = {});

    // Loads the value's declared dependencies and has the entry hold them; one
    // that cannot load fails the commit and is named in the result.
    [[nodiscard]] AssetCommitResult<DataAssetHandle> CommitTyped(AssetStaging&& staged, AssetSystem& assets);
    [[nodiscard]] std::size_t ParsedSourcesHeld() const { return ParsedSources.size(); }
    static constexpr std::size_t ParsedSourceCapacity = 64;
    [[nodiscard]] AssetReloadOutcome CommitReload(AssetStaging&& staged, AssetSystem& assets);

    // Refuses a value that declares dependencies.
    [[nodiscard]] DataAssetHandle CommitTyped(AssetStaging&& staged);
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    struct PendingComposition
    {
        JsonValue Root;
    };

    struct ParsedSource
    {
        std::uint64_t ContentHash = 0;
        std::uint64_t ReadAtCommit = 0;
        JsonValue Root;
    };

    [[nodiscard]] AssetStaging StageDefinition(const AssetRecord& record, const JsonValue& root,
                                               const AssetPathsById& current, const DataSourceReader& read);
    [[nodiscard]] std::string ComposePending(AssetStaging& staged, AssetSystem& assets);
    [[nodiscard]] DataSourceRead ReadSource(std::string_view path, AssetSystem& assets);
    void TrimParsedSources();
    // The dependency that did not load, or empty when every one did.
    [[nodiscard]] std::string LoadDependencies(const AssetStaging& staged, AssetSystem& assets,
                                               std::vector<AssetLease>& out);
    [[nodiscard]] DataAssetHandle Commit(AssetStaging&& staged, std::vector<AssetLease> dependencies);
    [[nodiscard]] bool Reload(AssetStaging&& staged, std::vector<AssetLease> dependencies);

    Logger& Log;
    DataAssetTypeRegistry* Types = nullptr;
    DataSchemaRegistry* Schemas = nullptr;
    DataAssetCache* Cache = nullptr;
    // Paths whose commit is loading its dependencies, outermost first.
    std::vector<std::string> Committing;
    std::unordered_map<std::string, ParsedSource> ParsedSources;
    std::uint64_t Compositions = 0;
};
