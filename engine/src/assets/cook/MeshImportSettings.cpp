#include <assets/cook/MeshImportSettings.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <cmath>
#include <format>
#include <optional>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    bool ParseInput(std::string_view clip, const AnimationClipEvent& event, const std::string& name,
                    const JsonValue& value, VerbBindingArgument& out, std::string* error)
    {
        const auto fail = [&](std::string_view why) {
            return Fail(error, std::format("clip '{}' event {} input '{}': {}", clip, event.Key, name, why));
        };
        if (!value.IsObject() || value.Size() != 1)
            return fail("expected {\"const\": ...} or {\"tag\": ...}");
        out.Key = name;
        if (const JsonValue* constant = value.Find("const"))
        {
            out.Source = VerbArgumentSource::Literal;
            out.Literal = *constant;
            return true;
        }
        if (const JsonValue* tag = value.Find("tag"))
        {
            if (!tag->IsString())
                return fail("a tag is its name, as a string");
            out.Source = VerbArgumentSource::Tag;
            out.Text = tag->AsString();
            return true;
        }
        return fail("an event supplies only constants and tags; asset and entity references are constants "
                    "on the binding");
    }

    bool ParseEvent(std::string_view clip, const JsonValue& value, AnimationClipEvent& out, std::string* error)
    {
        if (!value.IsObject())
            return Fail(error, std::format("clip '{}': an event must be an object", clip));
        for (const auto& [key, field] : value.AsObject())
        {
            const auto fail = [&](std::string_view why) {
                return Fail(error, std::format("clip '{}' event field '{}': {}", clip, key, why));
            };
            if (key == "key")
            {
                double whole = 0.0;
                if (!field.IsNumber() || std::modf(field.AsNumber(), &whole) != 0.0 || field.AsNumber() < 1.0
                    || field.AsNumber() > 4294967295.0)
                    return fail("expected a positive 32-bit integer");
                out.Key = static_cast<uint32_t>(field.AsNumber());
            }
            else if (key == "name")
            {
                if (!field.IsString())
                    return fail("expected a string");
                out.Name = field.AsString();
            }
            else if (key == "time")
            {
                if (!field.IsNumber())
                    return fail("expected a number");
                out.Time = static_cast<float>(field.AsNumber());
            }
            else if (key == "binding")
            {
                if (!field.IsString())
                    return fail("expected a binding key");
                out.Binding = field.AsString();
            }
            else if (key == "scope")
            {
                if (field.IsString() && field.AsString() == "cosmetic")
                    out.Scope = AnimEventScope::Cosmetic;
                else if (field.IsString() && field.AsString() == "gameplay")
                    out.Scope = AnimEventScope::Gameplay;
                else
                    return fail("expected \"cosmetic\" or \"gameplay\"");
            }
            else if (key == "min_weight")
            {
                if (!field.IsNumber())
                    return fail("expected a number");
                out.MinWeight = static_cast<float>(field.AsNumber());
            }
            else if (key != "inputs")
            {
                return fail("not an event field");
            }
        }
        if (const JsonValue* inputs = value.Find("inputs"))
        {
            if (!inputs->IsObject())
                return Fail(error, std::format("clip '{}' event {}: inputs must be an object", clip, out.Key));
            for (const auto& [name, input] : inputs->AsObject())
            {
                VerbBindingArgument argument;
                if (!ParseInput(clip, out, name, input, argument, error))
                    return false;
                out.Inputs.push_back(std::move(argument));
            }
        }
        std::string why;
        if (!ValidateAnimationClipEvent(out, &why))
            return Fail(error, std::format("clip '{}': {}", clip, why));
        return true;
    }

    JsonValue EventJson(const AnimationClipEvent& event)
    {
        JsonValue::Object object;
        object.emplace_back("key", JsonValue(static_cast<double>(event.Key)));
        if (!event.Name.empty())
            object.emplace_back("name", JsonValue(event.Name));
        object.emplace_back("time", JsonValue(static_cast<double>(event.Time)));
        object.emplace_back("binding", JsonValue(event.Binding));
        object.emplace_back("scope", JsonValue(std::string(AnimEventScopeName(event.Scope))));
        if (event.MinWeight.has_value())
            object.emplace_back("min_weight", JsonValue(static_cast<double>(*event.MinWeight)));
        if (!event.Inputs.empty())
        {
            JsonValue::Object inputs;
            for (const VerbBindingArgument& input : event.Inputs)
            {
                JsonValue::Object wrapper;
                if (input.Source == VerbArgumentSource::Tag)
                    wrapper.emplace_back("tag", JsonValue(input.Text));
                else
                    wrapper.emplace_back("const", input.Literal);
                inputs.emplace_back(input.Key, JsonValue(std::move(wrapper)));
            }
            object.emplace_back("inputs", JsonValue(std::move(inputs)));
        }
        return JsonValue(std::move(object));
    }
}

std::optional<MeshClipSource> MeshClipSourceOf(std::string_view clipPath)
{
    constexpr std::string_view kScheme = "asset://";
    if (!clipPath.starts_with(kScheme))
        return std::nullopt;
    const std::size_t hash = clipPath.find('#');
    if (hash == std::string_view::npos)
        return std::nullopt;
    const std::string_view fragment = clipPath.substr(hash + 1);
    if (!fragment.starts_with(kMeshClipFragmentPrefix) || fragment.size() == kMeshClipFragmentPrefix.size())
        return std::nullopt;
    return MeshClipSource{ std::string(clipPath.substr(kScheme.size(), hash - kScheme.size())),
                           std::string(fragment.substr(kMeshClipFragmentPrefix.size())) };
}

bool ParseMeshImportSettings(std::span<const std::byte> bytes, MeshImportSettings& out, std::string* error)
{
    out = MeshImportSettings{};
    if (bytes.empty())
        return true;

    JsonParseError parseError;
    const std::optional<JsonValue> json =
        JsonParse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &parseError);
    if (!json)
        return Fail(error, std::format("import settings JSON parse error at {}: {}", parseError.Position,
                                       parseError.Message));
    if (!json->IsObject())
        return Fail(error, "import settings must be a JSON object");

    for (const auto& [key, value] : json->AsObject())
    {
        if (key == "version")
        {
            if (!value.IsNumber() || value.AsNumber() != 1.0)
                return Fail(error, "unsupported import settings version");
            continue;
        }
        if (key != "clips")
            return Fail(error, std::format("'{}' is not a mesh import setting", key));
        if (!value.IsObject())
            return Fail(error, "'clips' must be an object keyed by clip name");

        for (const auto& [clip, settings] : value.AsObject())
        {
            if (!settings.IsObject())
                return Fail(error, std::format("clip '{}' must be an object", clip));
            std::vector<AnimationClipEvent>& events = out.ClipEvents[clip];
            for (const auto& [field, fieldValue] : settings.AsObject())
            {
                if (field != "events")
                    return Fail(error, std::format("clip '{}': '{}' is not a clip setting", clip, field));
                if (!fieldValue.IsArray())
                    return Fail(error, std::format("clip '{}': events must be an array", clip));
                for (const JsonValue& eventValue : fieldValue.AsArray())
                {
                    AnimationClipEvent event;
                    if (!ParseEvent(clip, eventValue, event, error))
                        return false;
                    for (const AnimationClipEvent& earlier : events)
                        if (earlier.Key == event.Key)
                            return Fail(error, std::format("clip '{}': event key {} is used twice", clip, event.Key));
                    events.push_back(std::move(event));
                }
            }
        }
    }
    return true;
}

std::string WriteMeshImportSettings(const MeshImportSettings& settings)
{
    JsonValue::Object root;
    root.emplace_back("version", JsonValue(1.0));
    if (!settings.ClipEvents.empty())
    {
        JsonValue::Object clips;
        for (const auto& [clip, events] : settings.ClipEvents)
        {
            JsonValue::Array eventArray;
            for (const AnimationClipEvent& event : events)
                eventArray.push_back(EventJson(event));
            JsonValue::Object clipObject;
            clipObject.emplace_back("events", JsonValue(std::move(eventArray)));
            clips.emplace_back(clip, JsonValue(std::move(clipObject)));
        }
        root.emplace_back("clips", JsonValue(std::move(clips)));
    }
    return JsonStringify(JsonValue(std::move(root)), true) + "\n";
}
