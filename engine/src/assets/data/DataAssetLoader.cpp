#include <assets/data/DataAssetLoader.h>

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetSource.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>

DataAssetLoader::DataAssetLoader(LoggingProvider& logging,
                                 DataAssetTypeRegistry* types,
                                 DataSchemaRegistry* schemas,
                                 DataAssetCache* cache)
    : Log(logging.GetLogger<DataAssetLoader>())
    , Types(types)
    , Schemas(schemas)
    , Cache(cache)
{
}

AssetStaging DataAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source,
                                         const AssetPathsById& current)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read data asset source for '{}'", record.Path);
        return staging;
    }

    JsonParseError jsonError;
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::optional<JsonValue> root = JsonParse(text, &jsonError);
    if (!root.has_value())
    {
        staging.Error = std::format("data JSON parse error at {}: {}",
                                    jsonError.Position, jsonError.Message);
        return staging;
    }
    return StageRoot(record, *root, current);
}

AssetStaging DataAssetLoader::StageRoot(const AssetRecord& record, const JsonValue& root,
                                        const AssetPathsById& current, const DataSourceReader& read)
{
    AssetStaging staging;
    staging.Record = record;
    if (Types == nullptr || Cache == nullptr)
    {
        staging.Error = "structured data services are not configured";
        return staging;
    }
    if (IsAbstractDataDefinition(root))
    {
        staging.Error = std::format("'{}' is abstract; extend or include it", record.Path);
        return staging;
    }
    const bool composes = root.IsObject() && (root.Find("extends") != nullptr || root.Find("includes") != nullptr);
    if (composes && !read)
    {
        staging.Payload = PendingComposition{ root };
        return staging;
    }
    return StageDefinition(record, root, current, read);
}

AssetStaging DataAssetLoader::StageDefinition(const AssetRecord& record, const JsonValue& root,
                                              const AssetPathsById& current, const DataSourceReader& read)
{
    AssetStaging staging;
    staging.Record = record;
    DataDefinitionStaging staged = StageDataDefinition(
        root, DataDefinitionContext{ .Types = *Types, .Schemas = Schemas, .Current = current, .Read = read,
                                     .Asset = record.Path });
    if (!staged.Ok())
    {
        staging.Error = FormatDataValidationErrors(staged.Errors);
        return staging;
    }
    CompiledDataAsset payload;
    payload.TypeName = std::move(staged.TypeName);
    payload.Version = staged.Version;
    payload.Value = std::move(staged.Value);
    payload.Sources = std::move(staged.Sources);
    staging.Dependencies = std::move(staged.Dependencies);
    staging.Payload = std::move(payload);
    return staging;
}

std::string DataAssetLoader::ComposePending(AssetStaging& staged, AssetSystem& assets)
{
    const PendingComposition* pending = std::any_cast<PendingComposition>(&staged.Payload);
    if (pending == nullptr)
        return {};
    std::unordered_map<std::string, JsonValue> sources;
    const DataSourceReader read = [&](std::string_view path) -> const JsonValue* {
        if (const auto found = sources.find(std::string(path)); found != sources.end())
            return &found->second;
        const AssetRecord* record = assets.Resolve(path, AssetType::Data);
        std::vector<std::byte> bytes;
        if (record == nullptr || !ReadAssetBytes(assets.DefaultSource(), *record, bytes))
            return nullptr;
        std::optional<JsonValue> root =
            JsonParse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (!root.has_value())
            return nullptr;
        return &sources.emplace(std::string(path), std::move(*root)).first->second;
    };
    const JsonValue root = pending->Root;
    AssetStaging composed = StageDefinition(staged.Record, root, *assets.PathsById(), read);
    if (!composed.IsValid())
        return composed.Error;
    staged = std::move(composed);
    return {};
}

std::string DataAssetLoader::LoadDependencies(const AssetStaging& staged, AssetSystem& assets,
                                              std::vector<AssetLease>& out)
{
    // A dependency still being committed further up this stack is a cycle.
    // The asynchronous preloader refuses one before it stages anything; the
    // synchronous path would otherwise recurse until the stack ran out.
    if (std::find(Committing.begin(), Committing.end(), staged.Record.Path) != Committing.end())
    {
        Log.Error("DataAssetLoader: dependency cycle through '{}'", staged.Record.Path);
        return staged.Record.Path;
    }
    Committing.push_back(staged.Record.Path);
    struct Unwind
    {
        std::vector<std::string>& Stack;
        ~Unwind() { Stack.pop_back(); }
    } unwind{ Committing };

    out.reserve(staged.Dependencies.size());
    for (const AssetRef& declared : staged.Dependencies)
    {
        // As the registry knows it: a reference the compile could not type (a
        // verb binding's asset, before any verb says its kind) loads as what is
        // at its path, and the binding's compile checks that kind against the verb.
        const AssetRef dependency = assets.Current(declared);
        if (std::find(Committing.begin(), Committing.end(), dependency.Path) != Committing.end())
        {
            Log.Error("DataAssetLoader: dependency cycle between '{}' and '{}'",
                      staged.Record.Path, dependency.Path);
            return dependency.Path;
        }
        AssetLease lease = assets.LoadLease(dependency.Path, dependency.Type);
        if (!lease)
        {
            Log.Error("DataAssetLoader: '{}' depends on '{}', which did not load",
                      staged.Record.Path, dependency.Path);
            return dependency.Path;
        }
        out.push_back(std::move(lease));
    }
    return {};
}

AssetCommitResult<DataAssetHandle> DataAssetLoader::CommitTyped(AssetStaging&& staged, AssetSystem& assets)
{
    if (std::string failed = ComposePending(staged, assets); !failed.empty())
        return AssetCommitResult<DataAssetHandle>::Failed(std::move(failed));
    std::vector<AssetLease> dependencies;
    if (staged.IsValid())
        if (std::string failed = LoadDependencies(staged, assets, dependencies); !failed.empty())
            return AssetCommitResult<DataAssetHandle>::DependencyFailed(std::move(failed));
    return Commit(std::move(staged), std::move(dependencies));
}

AssetReloadOutcome DataAssetLoader::CommitReload(AssetStaging&& staged, AssetSystem& assets)
{
    if (std::string failed = ComposePending(staged, assets); !failed.empty())
        return AssetReloadOutcome::Failed(std::move(failed));
    std::vector<AssetLease> dependencies;
    if (staged.IsValid())
        if (std::string failed = LoadDependencies(staged, assets, dependencies); !failed.empty())
            return AssetReloadOutcome::Failed(std::format("depends on '{}', which did not load", failed));
    return Reload(std::move(staged), std::move(dependencies));
}

DataAssetHandle DataAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (std::any_cast<PendingComposition>(&staged.Payload) != nullptr)
    {
        Log.Error("DataAssetLoader: '{}' extends or includes another definition and must commit through the "
                  "asset system",
                  staged.Record.Path);
        return {};
    }
    if (!staged.Dependencies.empty())
    {
        Log.Error("DataAssetLoader: '{}' declares dependencies and must commit through the "
                  "asset system",
                  staged.Record.Path);
        return {};
    }
    return Commit(std::move(staged), {});
}

bool DataAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (std::any_cast<PendingComposition>(&staged.Payload) != nullptr)
        return false;
    if (!staged.Dependencies.empty())
    {
        Log.Error("DataAssetLoader: '{}' declares dependencies and must reload through the "
                  "asset system",
                  staged.Record.Path);
        return false;
    }
    return Reload(std::move(staged), {});
}

DataAssetHandle DataAssetLoader::Commit(AssetStaging&& staged, std::vector<AssetLease> dependencies)
{
    if (!staged.IsValid())
    {
        Log.Error("DataAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }
    if (Cache == nullptr)
    {
        Log.Error("DataAssetLoader: missing DataAssetCache for '{}'", staged.Record.Path);
        return {};
    }

    CompiledDataAsset* compiled = std::any_cast<CompiledDataAsset>(&staged.Payload);
    if (compiled == nullptr || compiled->Value == nullptr)
    {
        Log.Error("DataAssetLoader: invalid compiled payload for '{}'", staged.Record.Path);
        return {};
    }

    DataAssetHandle handle = Cache->Register(staged.Record.Path,
                                             std::move(compiled->TypeName),
                                             std::move(compiled->Value),
                                             std::move(dependencies),
                                             std::move(compiled->Sources));
    if (!handle.IsValid())
        Log.Error("DataAssetLoader: failed to register '{}'", staged.Record.Path);
    return handle;
}

bool DataAssetLoader::Reload(AssetStaging&& staged, std::vector<AssetLease> dependencies)
{
    if (!staged.IsValid() || Cache == nullptr)
        return false;

    CompiledDataAsset* compiled = std::any_cast<CompiledDataAsset>(&staged.Payload);
    if (compiled == nullptr || compiled->Value == nullptr)
        return false;

    return Cache->ReloadInPlace(staged.Record.Path,
                                compiled->TypeName,
                                std::move(compiled->Value),
                                std::move(dependencies),
                                std::move(compiled->Sources));
}
