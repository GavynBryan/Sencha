#include "CookReceipt.h"

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

namespace
{
    constexpr std::uint32_t kReceiptVersion = 1;

    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    std::string HashText(std::uint64_t value)
    {
        return std::format("{:016x}", value);
    }

    bool ParseHash(const JsonValue* value, std::uint64_t& out)
    {
        if (value == nullptr || !value->IsString() || value->AsString().size() != 16)
            return false;
        const std::string& text = value->AsString();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out, 16);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    JsonValue WriteArtifact(const CookedArtifact& artifact)
    {
        return JsonValue(JsonValue::Object{
            { "path", JsonValue(artifact.Path) },
            { "file", JsonValue(artifact.FileRelPath) },
            { "type", JsonValue(std::string(AssetTypeToString(artifact.Type))) },
            { "hash", JsonValue(HashText(artifact.ContentHash)) },
        });
    }

    bool ReadArtifact(const JsonValue& value, CookedArtifact& out, std::string* error)
    {
        if (!value.IsObject())
            return Fail(error, "cook receipt artifact must be an object");
        const JsonValue* path = value.Find("path");
        const JsonValue* file = value.Find("file");
        const JsonValue* type = value.Find("type");
        if (path == nullptr || !path->IsString() || !IsValidAssetPath(path->AsString()))
            return Fail(error, "cook receipt artifact path is invalid");
        if (file == nullptr || !file->IsString() || file->AsString().empty())
            return Fail(error, "cook receipt artifact file is invalid");
        AssetType assetType = AssetType::Unknown;
        if (type == nullptr || !type->IsString()
            || !AssetTypeFromString(type->AsString(), assetType))
            return Fail(error, "cook receipt artifact type is invalid");
        std::uint64_t hash = 0;
        if (!ParseHash(value.Find("hash"), hash))
            return Fail(error, "cook receipt artifact hash is invalid");
        out = CookedArtifact{ path->AsString(), file->AsString(), assetType, hash };
        return true;
    }
}

const CookStepReceipt* FindCookStepReceipt(const DocumentCookReceipt& receipt,
                                           std::string_view stepId)
{
    const auto found = std::find_if(receipt.Steps.begin(), receipt.Steps.end(),
        [stepId](const CookStepReceipt& step) { return step.StepId == stepId; });
    return found != receipt.Steps.end() ? &*found : nullptr;
}

void PutCookStepReceipt(DocumentCookReceipt& receipt, CookStepReceipt step)
{
    const auto found = std::find_if(receipt.Steps.begin(), receipt.Steps.end(),
        [&step](const CookStepReceipt& existing) { return existing.StepId == step.StepId; });
    if (found == receipt.Steps.end())
        receipt.Steps.push_back(std::move(step));
    else
        *found = std::move(step);
}

JsonValue WriteDocumentCookReceipt(const DocumentCookReceipt& receipt)
{
    JsonValue::Array outputs;
    for (const std::string& family : receipt.PublishedOutputFamilies)
        outputs.push_back(JsonValue(family));

    std::vector<const CookStepReceipt*> ordered;
    ordered.reserve(receipt.Steps.size());
    for (const CookStepReceipt& step : receipt.Steps)
        ordered.push_back(&step);
    std::ranges::sort(ordered,
        [](const CookStepReceipt* a, const CookStepReceipt* b)
        { return a->StepId < b->StepId; });

    JsonValue::Array steps;
    for (const CookStepReceipt* step : ordered)
    {
        JsonValue::Array dependencies;
        for (const auto& [id, fingerprint] : step->Dependencies)
            dependencies.push_back(JsonValue(JsonValue::Object{
                { "step", JsonValue(id) },
                { "fingerprint", JsonValue(HashText(fingerprint)) },
            }));
        JsonValue::Array artifacts;
        for (const CookedArtifact& artifact : step->Artifacts)
            artifacts.push_back(WriteArtifact(artifact));
        steps.push_back(JsonValue(JsonValue::Object{
            { "id", JsonValue(step->StepId) },
            { "version", JsonValue(static_cast<double>(step->Version)) },
            { "fingerprint", JsonValue(HashText(step->InputFingerprint)) },
            { "dependencies", JsonValue(std::move(dependencies)) },
            { "artifacts", JsonValue(std::move(artifacts)) },
            { "metadata", step->Metadata },
            { "duration_ms", JsonValue(step->DurationMilliseconds) },
        }));
    }

    return JsonValue(JsonValue::Object{
        { "version", JsonValue(static_cast<double>(kReceiptVersion)) },
        { "target", JsonValue(receipt.Target) },
        { "profile", JsonValue(receipt.PublishedProfileId) },
        { "published_fingerprint", JsonValue(HashText(receipt.PublishedFingerprint)) },
        { "published_outputs", JsonValue(std::move(outputs)) },
        { "steps", JsonValue(std::move(steps)) },
    });
}

bool ReadDocumentCookReceipt(const JsonValue& value,
                             DocumentCookReceipt& out,
                             std::string* error)
{
    if (!value.IsObject())
        return Fail(error, "cook receipt root must be an object");
    const JsonValue* version = value.Find("version");
    if (version == nullptr || !version->IsNumber()
        || static_cast<std::uint32_t>(version->AsNumber()) != kReceiptVersion)
        return Fail(error, "unsupported cook receipt version");
    const JsonValue* target = value.Find("target");
    const JsonValue* profile = value.Find("profile");
    const JsonValue* outputs = value.Find("published_outputs");
    const JsonValue* steps = value.Find("steps");
    if (target == nullptr || !target->IsString() || target->AsString().empty())
        return Fail(error, "cook receipt target is invalid");
    if (profile == nullptr || !profile->IsString())
        return Fail(error, "cook receipt profile is invalid");
    if (outputs == nullptr || !outputs->IsArray())
        return Fail(error, "cook receipt published_outputs is invalid");
    if (steps == nullptr || !steps->IsArray())
        return Fail(error, "cook receipt steps is invalid");

    DocumentCookReceipt receipt;
    receipt.Target = target->AsString();
    receipt.PublishedProfileId = profile->AsString();
    if (!ParseHash(value.Find("published_fingerprint"), receipt.PublishedFingerprint))
        return Fail(error, "cook receipt published fingerprint is invalid");
    for (const JsonValue& output : outputs->AsArray())
    {
        if (!output.IsString())
            return Fail(error, "cook receipt output family must be a string");
        receipt.PublishedOutputFamilies.push_back(output.AsString());
    }
    for (const JsonValue& stepValue : steps->AsArray())
    {
        if (!stepValue.IsObject())
            return Fail(error, "cook receipt step must be an object");
        const JsonValue* id = stepValue.Find("id");
        const JsonValue* stepVersion = stepValue.Find("version");
        const JsonValue* dependencies = stepValue.Find("dependencies");
        const JsonValue* artifacts = stepValue.Find("artifacts");
        if (id == nullptr || !id->IsString() || id->AsString().empty())
            return Fail(error, "cook receipt step id is invalid");
        if (stepVersion == nullptr || !stepVersion->IsNumber())
            return Fail(error, "cook receipt step version is invalid");
        if (dependencies == nullptr || !dependencies->IsArray())
            return Fail(error, "cook receipt dependencies is invalid");
        if (artifacts == nullptr || !artifacts->IsArray())
            return Fail(error, "cook receipt artifacts is invalid");

        CookStepReceipt step;
        step.StepId = id->AsString();
        step.Version = static_cast<std::uint32_t>(stepVersion->AsNumber());
        if (!ParseHash(stepValue.Find("fingerprint"), step.InputFingerprint))
            return Fail(error, "cook receipt step fingerprint is invalid");
        for (const JsonValue& dependency : dependencies->AsArray())
        {
            if (!dependency.IsObject())
                return Fail(error, "cook receipt dependency must be an object");
            const JsonValue* dependencyId = dependency.Find("step");
            std::uint64_t fingerprint = 0;
            if (dependencyId == nullptr || !dependencyId->IsString()
                || !ParseHash(dependency.Find("fingerprint"), fingerprint))
                return Fail(error, "cook receipt dependency is invalid");
            step.Dependencies.emplace_back(dependencyId->AsString(), fingerprint);
        }
        for (const JsonValue& artifact : artifacts->AsArray())
        {
            CookedArtifact parsed;
            if (!ReadArtifact(artifact, parsed, error))
                return false;
            step.Artifacts.push_back(std::move(parsed));
        }
        if (const JsonValue* metadata = stepValue.Find("metadata"))
            step.Metadata = *metadata;
        if (const JsonValue* duration = stepValue.Find("duration_ms");
            duration != nullptr && duration->IsNumber())
            step.DurationMilliseconds = duration->AsNumber();
        receipt.Steps.push_back(std::move(step));
    }
    out = std::move(receipt);
    return true;
}

bool LoadDocumentCookReceipt(const std::filesystem::path& path,
                             DocumentCookReceipt& out,
                             std::string* error)
{
    std::ifstream file(path);
    if (!file.is_open())
        return Fail(error, "could not open cook receipt '" + path.generic_string() + "'");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    JsonParseError parseError;
    const std::optional<JsonValue> parsed = JsonParse(buffer.str(), &parseError);
    if (!parsed.has_value())
        return Fail(error, "cook receipt JSON parse error at "
            + std::to_string(parseError.Position) + ": " + parseError.Message);
    return ReadDocumentCookReceipt(*parsed, out, error);
}

bool SaveDocumentCookReceipt(const std::filesystem::path& path,
                             const DocumentCookReceipt& receipt,
                             std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return Fail(error, "could not create cook receipt directory");
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file.is_open())
            return Fail(error, "could not write cook receipt '" + temporary.generic_string() + "'");
        file << JsonStringify(WriteDocumentCookReceipt(receipt), true);
        if (!file.good())
            return Fail(error, "cook receipt write failed '" + temporary.generic_string() + "'");
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    return !ec || Fail(error, "could not replace cook receipt '" + path.generic_string() + "'");
}

