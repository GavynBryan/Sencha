#include <assets/data/DataDefinitionStaging.h>

#include <core/composition/JsonComposition.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace
{
    constexpr std::size_t kMaxCompositionDepth = 16;

    void AddError(std::vector<DataValidationError>& errors, std::string path, std::string message)
    {
        errors.push_back(DataValidationError{ std::move(path), std::move(message) });
    }

    // "$" and "$.x" are paths inside `data`; the envelope's are "$.data" and "$.data.x".
    std::string InEnvelope(std::string_view dataPath)
    {
        return dataPath.size() <= 1 ? std::string("$.data") : "$.data" + std::string(dataPath.substr(1));
    }

    std::string InData(std::string_view envelopePath)
    {
        if (envelopePath == "$.data")
            return "$";
        return envelopePath.starts_with("$.data") ? "$" + std::string(envelopePath.substr(6)) : std::string();
    }

    // A reference's asset path, through the id when this build knows it.
    std::optional<std::string> ReferencedPath(const JsonValue& value, const AssetPathsById& current)
    {
        const JsonValue resolved = ResolveAssetRefs(value, current);
        if (!resolved.IsString() || !resolved.AsString().starts_with("asset://"))
            return std::nullopt;
        return resolved.AsString();
    }

    struct Source
    {
        std::string Path;
        std::string Field;
        bool Fragment = false;
    };

    class Stager
    {
    public:
        explicit Stager(const DataDefinitionContext& context)
            : Context(context)
        {
        }

        DataDefinitionStaging Stage(const JsonValue& root, std::string_view asset, bool provenance)
        {
            DataDefinitionStaging staging;
            if (!CheckEnvelope(root, staging))
                return staging;
            const JsonValue& data = *root.Find("data");
            const DataSchema* schema = Context.Schemas != nullptr ? Context.Schemas->Find(staging.TypeName) : nullptr;

            std::vector<Source> bases;
            std::vector<Source> includes;
            if (!ReadSources(root, bases, includes, staging))
                return staging;
            if (bases.empty() && includes.empty())
            {
                staging.Data = data;
                return staging;
            }

            staging.Composed = true;
            Stack.emplace_back(asset);
            std::vector<DataDefinitionStaging> composedBase;
            std::vector<DataDefinitionStaging> composedIncludes;
            for (const Source& base : bases)
                if (std::optional<DataDefinitionStaging> composed = StageSource(base, staging, provenance))
                    composedBase.push_back(std::move(*composed));
            for (const Source& include : includes)
                if (std::optional<DataDefinitionStaging> composed = StageSource(include, staging, provenance))
                    composedIncludes.push_back(std::move(*composed));
            Stack.pop_back();
            if (!staging.Ok())
                return staging;

            for (const auto* group : { &composedBase, &composedIncludes })
                for (const DataDefinitionStaging& source : *group)
                    for (const std::string& path : source.Sources)
                        if (std::ranges::find(staging.Sources, path) == staging.Sources.end())
                            staging.Sources.push_back(path);
            for (const auto* group : { &bases, &includes })
                for (const Source& source : *group)
                    if (std::ranges::find(staging.Sources, source.Path) == staging.Sources.end())
                        staging.Sources.push_back(source.Path);

            const CompositionPolicy policy{ .Schema = schema != nullptr ? &schema->Root : nullptr,
                                            .Null = CompositionNull::Removes,
                                            .Provenance = provenance };
            std::optional<CompositionLayerInput<JsonValue>> base;
            if (!composedBase.empty())
                base = CompositionLayerInput<JsonValue>{ &composedBase.front().Data, bases.front().Path,
                                                         &composedBase.front().Composition.Provenance };
            std::vector<CompositionLayerInput<JsonValue>> layers;
            for (std::size_t i = 0; i < composedIncludes.size(); ++i)
                layers.push_back({ &composedIncludes[i].Data, includes[i].Path, &composedIncludes[i].Composition.Provenance });
            const CompositionLayerInput<JsonValue> local{ &data, asset, nullptr };
            staging.Data = ComposeLayers<JsonValue>(base ? &*base : nullptr, layers, local, policy, staging.Composition);
            for (const CompositionDiagnostic& diagnostic : staging.Composition.Diagnostics)
            {
                std::string message = diagnostic.Message;
                if (diagnostic.Asset != asset)
                    message += std::format(" (in '{}')", diagnostic.Asset);
                AddError(staging.Errors, InEnvelope(diagnostic.Path), std::move(message));
            }
            return staging;
        }

    private:
        bool CheckEnvelope(const JsonValue& root, DataDefinitionStaging& staging)
        {
            if (!root.IsObject())
            {
                AddError(staging.Errors, "$", "data asset root must be an object");
                return false;
            }
            const JsonValue* type = root.Find("type");
            const JsonValue* version = root.Find("version");
            const JsonValue* data = root.Find("data");
            if (type == nullptr || !type->IsString())
                AddError(staging.Errors, "$.type", "required string field is missing");
            if (version == nullptr || !version->IsNumber() || version->AsNumber() < 1.0
                || std::floor(version->AsNumber()) != version->AsNumber())
                AddError(staging.Errors, "$.version", "required positive integer field is missing");
            if (data == nullptr)
                AddError(staging.Errors, "$.data", "required data field is missing");
            if (const JsonValue* abstract = root.Find("abstract"); abstract != nullptr && !abstract->IsBool())
                AddError(staging.Errors, "$.abstract", "expected boolean");
            if (!staging.Ok())
                return false;

            staging.TypeName = type->AsString();
            staging.Version = static_cast<std::uint32_t>(version->AsNumber());
            staging.Abstract = IsAbstractDataDefinition(root);
            const DataAssetTypeRegistration* registration = Context.Types.Find(staging.TypeName);
            if (registration == nullptr)
            {
                AddError(staging.Errors, "$.type", std::format("unknown data asset subtype '{}'", staging.TypeName));
                return false;
            }
            if (registration->CurrentVersion != staging.Version)
            {
                AddError(staging.Errors, "$.version",
                         std::format("data subtype '{}' supports version {}, file uses {}", staging.TypeName,
                                     registration->CurrentVersion, staging.Version));
                return false;
            }
            return true;
        }

        bool ReadSources(const JsonValue& root, std::vector<Source>& bases, std::vector<Source>& includes,
                         DataDefinitionStaging& staging)
        {
            if (const JsonValue* extends = root.Find("extends"))
            {
                if (std::optional<std::string> path = ReferencedPath(*extends, Context.Current))
                    bases.push_back({ std::move(*path), "$.extends" });
                else
                    AddError(staging.Errors, "$.extends", "expected an asset reference");
            }
            if (const JsonValue* list = root.Find("includes"))
            {
                if (!list->IsArray())
                {
                    AddError(staging.Errors, "$.includes", "expected a list of asset references");
                    return false;
                }
                for (std::size_t i = 0; i < list->Size(); ++i)
                {
                    const std::string field = std::format("$.includes[{}]", i);
                    if (std::optional<std::string> path = ReferencedPath(list->AsArray()[i], Context.Current))
                        includes.push_back({ std::move(*path), field, true });
                    else
                        AddError(staging.Errors, field, "expected an asset reference");
                }
            }
            return staging.Ok();
        }

        std::optional<DataDefinitionStaging> StageSource(const Source& source, DataDefinitionStaging& staging,
                                                         bool provenance)
        {
            if (std::ranges::find(Stack, source.Path) != Stack.end())
            {
                AddError(staging.Errors, source.Field,
                         std::format("'{}' extends or includes itself through this chain", source.Path));
                return std::nullopt;
            }
            if (Stack.size() >= kMaxCompositionDepth)
            {
                AddError(staging.Errors, source.Field,
                         std::format("composition is deeper than {} definitions", kMaxCompositionDepth));
                return std::nullopt;
            }
            const JsonValue* root = Context.Read ? Context.Read(source.Path) : nullptr;
            if (root == nullptr)
            {
                AddError(staging.Errors, source.Field, std::format("'{}' does not exist", source.Path));
                return std::nullopt;
            }
            if (source.Fragment && root->IsObject() && root->Find("extends") != nullptr)
            {
                AddError(staging.Errors, source.Field,
                         std::format("'{}' extends another definition, so what it inherits would land as what it "
                                     "wrote; include only fragments that do not extend",
                                     source.Path));
                return std::nullopt;
            }
            DataDefinitionStaging composed = Stage(*root, source.Path, provenance);
            if (!composed.Ok())
            {
                const DataValidationError& first = composed.Errors.front();
                AddError(staging.Errors, source.Field,
                         std::format("'{}' does not compose: {} {}", source.Path, first.Path, first.Message));
                return std::nullopt;
            }
            if (composed.TypeName != staging.TypeName)
            {
                AddError(staging.Errors, source.Field,
                         std::format("'{}' is a {}, not a {}", source.Path, composed.TypeName, staging.TypeName));
                return std::nullopt;
            }
            return composed;
        }

        const DataDefinitionContext& Context;
        std::vector<std::string> Stack;
    };

    // A composed definition's error at an index path, named by key and by the
    // asset that authored the value.
    DataValidationError Authored(const DataValidationError& error, const DataDefinitionStaging& staging,
                                 const CompositionProvenance* provenance, const DataSchema* schema,
                                 std::string_view asset)
    {
        const std::string dataPath = InData(error.Path);
        if (!staging.Composed || dataPath.empty() || schema == nullptr)
            return error;
        const std::string logical = CompositionLogicalPath(staging.Data, schema->Root, dataPath);
        DataValidationError authored{ InEnvelope(logical), error.Message };
        if (provenance != nullptr)
            if (const CompositionOrigin* origin = FindCompositionOrigin(*provenance, logical);
                origin != nullptr && origin->Asset != asset)
                authored.Message += std::format(" (inherited from '{}')", origin->Asset);
        return authored;
    }
}

DataDefinitionStaging StageDataDefinition(const JsonValue& root, const DataDefinitionContext& context)
{
    Stager stager(context);
    DataDefinitionStaging staging = stager.Stage(root, context.Asset, context.Provenance);
    if (!staging.Ok())
        return staging;

    const DataSchema* schema = context.Schemas != nullptr ? context.Schemas->Find(staging.TypeName) : nullptr;
    std::vector<DataValidationError> found;
    if (schema != nullptr)
    {
        std::vector<DataValidationError> schemaErrors;
        (void)ValidateDataAgainstSchema(staging.Data, *schema, schemaErrors,
                                        staging.Abstract ? DataValidationMode::Partial : DataValidationMode::Complete);
        for (const DataValidationError& error : schemaErrors)
            found.push_back({ InEnvelope(error.Path), error.Message });
    }

    // A compiler sees data its schema accepted, which is the only precondition it
    // can be written against; an abstract definition is never compiled.
    const DataAssetTypeRegistration* registration = context.Types.Find(staging.TypeName);
    if (found.empty() && !staging.Abstract)
    {
        DataAssetCompileResult compiled = CompileDataAsset(*registration, staging.Data, context.Current);
        if (compiled.IsValid())
        {
            staging.Value = std::move(compiled.Value);
            staging.Dependencies = std::move(compiled.Dependencies);
        }
        else
        {
            found.push_back(compiled.Error.empty()
                                ? DataValidationError{ "$.data", "subtype compiler returned no value" }
                                : SplitCompileError(std::move(compiled.Error)));
        }
    }
    if (found.empty())
        return staging;

    // Provenance is only worth computing for a definition that failed.
    const CompositionProvenance* provenance = nullptr;
    DataDefinitionStaging traced;
    if (staging.Composed)
    {
        if (context.Provenance)
            provenance = &staging.Composition.Provenance;
        else
        {
            DataDefinitionContext tracing = context;
            tracing.Provenance = true;
            traced = Stager(tracing).Stage(root, context.Asset, true);
            provenance = &traced.Composition.Provenance;
        }
    }
    for (const DataValidationError& error : found)
        staging.Errors.push_back(Authored(error, staging, provenance, schema, context.Asset));
    staging.Value.reset();
    staging.Dependencies.clear();
    return staging;
}

bool IsAbstractDataDefinition(const JsonValue& root)
{
    const JsonValue* abstract = root.IsObject() ? root.Find("abstract") : nullptr;
    return abstract != nullptr && abstract->IsBool() && abstract->AsBool();
}

DataValidationError SplitCompileError(std::string message)
{
    if (!message.starts_with("$."))
        return DataValidationError{ "$.data", std::move(message) };
    const auto pathEnd =
        message.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$[]");
    if (pathEnd == std::string::npos)
        return DataValidationError{ std::move(message), std::string() };
    std::string path = message.substr(0, pathEnd);
    std::string rest = message.substr(pathEnd);
    if (const auto textStart = rest.find_first_not_of(" \t"); textStart != std::string::npos)
        rest.erase(0, textStart);
    return DataValidationError{ std::move(path), std::move(rest) };
}
