#include "authoring/AnimationTraceImport.h"

#include <anim/AnimTrace.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
    std::string Describe(const JsonValue& value)
    {
        if (value.IsString())
            return value.AsString();
        if (value.IsNumber())
        {
            const double number = value.AsNumber();
            return number == static_cast<double>(static_cast<std::int64_t>(number))
                ? std::format("{}", static_cast<std::int64_t>(number))
                : std::format("{:.3g}", number);
        }
        if (value.IsObject())
        {
            std::string text;
            for (const auto& [key, field] : value.AsObject())
                text += (text.empty() ? "" : " ") + key + " " + Describe(field);
            return "(" + text + ")";
        }
        return JsonStringify(value);
    }

    std::string Text(const JsonValue& object, std::string_view key)
    {
        const JsonValue* value = object.Find(key);
        return value != nullptr && value->IsString() ? value->AsString() : std::string();
    }
}

std::optional<AnimationTrace> ReadAnimationTrace(const JsonValue& document, std::string& error)
{
    if (!document.IsObject() || Text(document, "type") != kAnimTraceType)
    {
        error = std::format("not an animation trace: its type is not '{}'", kAnimTraceType);
        return std::nullopt;
    }
    const JsonValue* version = document.Find("version");
    if (version == nullptr || !version->IsNumber() || version->AsNumber() != 1.0)
    {
        error = "this animation trace is a version this editor cannot read";
        return std::nullopt;
    }
    const JsonValue* records = document.Find("records");
    if (records == nullptr || !records->IsArray())
    {
        error = "the animation trace has no records";
        return std::nullopt;
    }

    AnimationTrace trace;
    trace.Entity = Text(document, "entity");
    trace.Rig = Text(document, "rig");
    trace.Captured = Text(document, "captured");
    if (const JsonValue* written = document.Find("records_written"); written != nullptr && written->IsNumber())
        trace.RecordsWritten = static_cast<std::uint64_t>(written->AsNumber());
    for (const JsonValue& record : records->AsArray())
    {
        if (!record.IsObject())
            continue;
        AnimationTraceRow row;
        for (const auto& [key, value] : record.AsObject())
        {
            if (key == "tick" && value.IsNumber())
                row.Tick = static_cast<std::uint64_t>(value.AsNumber());
            else if (key == "cause")
                row.Cause = Describe(value);
            else if (key == "layer_name")
                row.Layer = Describe(value);
            else if (key == "layer")
                row.Layer = row.Layer.empty() ? std::format("layer {}", Describe(value)) : row.Layer;
            else
                row.Detail += (row.Detail.empty() ? "" : ", ") + key + " " + Describe(value);
        }
        trace.Rows.push_back(std::move(row));
    }
    trace.RecordsWritten = std::max<std::uint64_t>(trace.RecordsWritten, trace.Rows.size());
    return trace;
}

std::optional<AnimationTrace> ReadAnimationTraceFile(const std::filesystem::path& file, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        error = std::format("could not open {}", file.string());
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    const std::optional<JsonValue> document = JsonParse(text.str());
    if (!document)
    {
        error = std::format("{} is not JSON", file.string());
        return std::nullopt;
    }
    return ReadAnimationTrace(*document, error);
}
