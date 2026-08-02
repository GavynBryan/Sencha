#include <assets/data/DataAssetLoader.h>

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>

#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace
{
    std::string FormatValidationErrors(const std::vector<DataValidationError>& errors)
    {
        std::string result;
        const size_t count = std::min<size_t>(errors.size(), 8);
        for (size_t index = 0; index < count; ++index)
        {
            if (!result.empty())
                result += "; ";
            result += std::format("{}: {}", errors[index].Path, errors[index].Message);
        }
        if (errors.size() > count)
            result += std::format("; {} more errors", errors.size() - count);
        return result;
    }
}

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

    if (Types == nullptr || Cache == nullptr)
    {
        staging.Error = "structured data services are not configured";
        return staging;
    }

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
    if (!root->IsObject())
    {
        staging.Error = "data asset root must be an object";
        return staging;
    }

    const JsonValue* typeValue = root->Find("type");
    const JsonValue* versionValue = root->Find("version");
    const JsonValue* dataValue = root->Find("data");
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
                                            FormatValidationErrors(errors));
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

DataAssetHandle DataAssetLoader::CommitTyped(AssetStaging&& staged)
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
                                             std::move(compiled->Value));
    if (!handle.IsValid())
        Log.Error("DataAssetLoader: failed to register '{}'", staged.Record.Path);
    return handle;
}

bool DataAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (!staged.IsValid() || Cache == nullptr)
        return false;

    CompiledDataAsset* compiled = std::any_cast<CompiledDataAsset>(&staged.Payload);
    if (compiled == nullptr || compiled->Value == nullptr)
        return false;

    return Cache->ReloadInPlace(staged.Record.Path,
                                compiled->TypeName,
                                std::move(compiled->Value));
}
