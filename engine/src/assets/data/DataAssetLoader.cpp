#include <assets/data/DataAssetLoader.h>

#include <assets/runtime/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
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

AssetStaging DataAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
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
    return StageRoot(record, *root);
}

AssetStaging DataAssetLoader::StageRoot(const AssetRecord& record, const JsonValue& root)
{
    AssetStaging staging;
    staging.Record = record;
    if (Types == nullptr || Cache == nullptr)
    {
        staging.Error = "structured data services are not configured";
        return staging;
    }
    if (!root.IsObject())
    {
        staging.Error = "data asset root must be an object";
        return staging;
    }

    const JsonValue* typeValue = root.Find("type");
    const JsonValue* versionValue = root.Find("version");
    const JsonValue* dataValue = root.Find("data");
    if (typeValue == nullptr || !typeValue->IsString())
    {
        staging.Error = "data asset requires string field 'type'";
        return staging;
    }
    if (versionValue == nullptr || !versionValue->IsNumber()
        || versionValue->AsNumber() < 1.0
        || std::floor(versionValue->AsNumber()) != versionValue->AsNumber())
    {
        staging.Error = "data asset requires positive integer field 'version'";
        return staging;
    }
    if (dataValue == nullptr)
    {
        staging.Error = "data asset requires field 'data'";
        return staging;
    }

    const std::string& typeName = typeValue->AsString();
    const uint32_t version = static_cast<uint32_t>(versionValue->AsNumber());
    const DataAssetTypeRegistration* registration = Types->Find(typeName);
    if (registration == nullptr)
    {
        staging.Error = std::format("unknown data asset subtype '{}'", typeName);
        return staging;
    }
    if (registration->CurrentVersion != version)
    {
        staging.Error = std::format("data subtype '{}' supports version {}, file uses {}",
                                    typeName, registration->CurrentVersion, version);
        return staging;
    }

    if (Schemas != nullptr)
    {
        if (const DataSchema* schema = Schemas->Find(typeName))
        {
            std::vector<DataValidationError> errors;
            if (!ValidateDataAgainstSchema(*dataValue, *schema, errors))
            {
                staging.Error = std::format("data schema validation failed: {}",
                                            FormatDataValidationErrors(errors));
                return staging;
            }
        }
    }

    DataAssetCompileResult compiled = registration->Compile(*dataValue);
    if (!compiled.IsValid())
    {
        staging.Error = compiled.Error.empty()
            ? std::format("data subtype '{}' compiler returned no value", typeName)
            : std::move(compiled.Error);
        return staging;
    }

    CompiledDataAsset payload;
    payload.TypeName = typeName;
    payload.Version = version;
    payload.Value = std::move(compiled.Value);
    staging.Dependencies = std::move(compiled.Dependencies);
    staging.Payload = std::move(payload);
    return staging;
}

bool DataAssetLoader::LoadDependencies(const AssetStaging& staged, AssetSystem& assets,
                                       std::vector<AssetLease>& out)
{
    // A dependency still being committed further up this stack is a cycle.
    // The asynchronous preloader refuses one before it stages anything; the
    // synchronous path would otherwise recurse until the stack ran out.
    if (std::find(Committing.begin(), Committing.end(), staged.Record.Path) != Committing.end())
    {
        Log.Error("DataAssetLoader: dependency cycle through '{}'", staged.Record.Path);
        return false;
    }
    Committing.push_back(staged.Record.Path);
    struct Unwind
    {
        std::vector<std::string>& Stack;
        ~Unwind() { Stack.pop_back(); }
    } unwind{ Committing };

    out.reserve(staged.Dependencies.size());
    for (const AssetRef& dependency : staged.Dependencies)
    {
        if (std::find(Committing.begin(), Committing.end(), dependency.Path) != Committing.end())
        {
            Log.Error("DataAssetLoader: dependency cycle between '{}' and '{}'",
                      staged.Record.Path, dependency.Path);
            return false;
        }
        AssetLease lease = assets.LoadLease(dependency.Path, dependency.Type);
        if (!lease)
        {
            Log.Error("DataAssetLoader: '{}' depends on '{}', which did not load",
                      staged.Record.Path, dependency.Path);
            return false;
        }
        out.push_back(std::move(lease));
    }
    return true;
}

DataAssetHandle DataAssetLoader::CommitTyped(AssetStaging&& staged, AssetSystem& assets)
{
    std::vector<AssetLease> dependencies;
    if (staged.IsValid() && !LoadDependencies(staged, assets, dependencies))
        return {};
    return Commit(std::move(staged), std::move(dependencies));
}

bool DataAssetLoader::CommitReload(AssetStaging&& staged, AssetSystem& assets)
{
    std::vector<AssetLease> dependencies;
    if (staged.IsValid() && !LoadDependencies(staged, assets, dependencies))
        return false;
    return Reload(std::move(staged), std::move(dependencies));
}

DataAssetHandle DataAssetLoader::CommitTyped(AssetStaging&& staged)
{
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
                                             std::move(dependencies));
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
                                std::move(dependencies));
}
