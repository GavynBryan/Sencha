#include "authoring/AnimationScenario.h"

#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
    constexpr std::array<std::string_view, 3> kLifetimeNames{ "held", "fixed", "impulse" };
    constexpr std::array<std::string_view, 5> kReasonNames{ "none", "released", "interrupted",
                                                            "failed", "superseded" };

    template <std::size_t N>
    std::optional<std::size_t> IndexOf(const std::array<std::string_view, N>& names,
                                       std::string_view name)
    {
        const auto it = std::find(names.begin(), names.end(), name);
        return it == names.end() ? std::nullopt
                                 : std::optional<std::size_t>(static_cast<std::size_t>(it - names.begin()));
    }

    JsonValue WriteValue(const AnimationScenarioValue& value)
    {
        switch (value.Type)
        {
        case AnimationScenarioValue::Kind::Bool: return JsonValue(value.Bool);
        case AnimationScenarioValue::Kind::Number: return JsonValue(value.Number);
        case AnimationScenarioValue::Kind::Name: return JsonValue(value.Name);
        }
        return JsonValue();
    }

    std::optional<AnimationScenarioValue> ReadValue(const JsonValue& value)
    {
        if (value.IsBool())
            return AnimationScenarioValue::FromBool(value.AsBool());
        if (value.IsNumber())
            return AnimationScenarioValue::FromNumber(value.AsNumber());
        if (value.IsString())
            return AnimationScenarioValue::FromName(value.AsString());
        return std::nullopt;
    }

    JsonValue WriteNamedValues(const std::vector<std::pair<std::string, AnimationScenarioValue>>& values)
    {
        JsonValue::Object object;
        for (const auto& [name, value] : values)
            object.emplace_back(name, WriteValue(value));
        return JsonValue(std::move(object));
    }

    // The keys the format owns, so everything else is preserved as unknown.
    bool IsKnown(std::string_view key, std::initializer_list<std::string_view> known)
    {
        return std::find(known.begin(), known.end(), key) != known.end();
    }

    struct Reader
    {
        std::string_view AssetPath;
        std::vector<AnimDiagnostic>& Diagnostics;

        void Error(std::string field, std::string message)
        {
            Diagnostics.push_back(AnimDiagnostic{ AnimDiagnosticSeverity::Error,
                                                  "anim.scenario.malformed",
                                                  std::string(AssetPath), std::move(field),
                                                  std::move(message) });
        }

        void Named(const JsonValue* object, const std::string& at,
                   std::vector<std::pair<std::string, AnimationScenarioValue>>& out)
        {
            if (object == nullptr)
                return;
            if (!object->IsObject())
            {
                Error(at, "Expected an object of names to values.");
                return;
            }
            for (const auto& [name, value] : object->AsObject())
            {
                std::optional<AnimationScenarioValue> read = ReadValue(value);
                if (!read)
                {
                    Error(std::format("{}.{}", at, name), "A value is a bool, a number or a tag name.");
                    continue;
                }
                out.emplace_back(name, std::move(*read));
            }
        }

        std::optional<AnimationScenarioAction> Action(const JsonValue& entry, const std::string& at)
        {
            if (!entry.IsObject())
            {
                Error(at, "An action is an object.");
                return std::nullopt;
            }
            AnimationScenarioAction action;
            const JsonValue* tick = entry.Find("tick");
            if (tick == nullptr || !tick->IsNumber() || tick->AsNumber() < 0.0)
            {
                Error(at + ".tick", "An action needs a tick of zero or more.");
                return std::nullopt;
            }
            action.Tick = static_cast<AnimTick>(tick->AsNumber());

            const auto text = [&](std::string_view key) -> const std::string* {
                const JsonValue* value = entry.Find(key);
                return value != nullptr && value->IsString() ? &value->AsString() : nullptr;
            };

            if (const std::string* fact = text("set"))
            {
                action.Kind = AnimationScenarioActionKind::SetFact;
                action.Fact = *fact;
                const JsonValue* value = entry.Find("value");
                std::optional<AnimationScenarioValue> read =
                    value != nullptr ? ReadValue(*value) : std::nullopt;
                if (!read)
                {
                    Error(at + ".value", "Setting a fact needs a bool, number or tag name.");
                    return std::nullopt;
                }
                action.Value = std::move(*read);
            }
            else if (const std::string* cleared = text("clear"))
            {
                action.Kind = AnimationScenarioActionKind::ClearFact;
                action.Fact = *cleared;
            }
            else if (const std::string* intent = text("issue"))
            {
                action.Kind = AnimationScenarioActionKind::IssueRequest;
                action.Intent = *intent;
                if (const std::string* lifetime = text("lifetime"))
                {
                    const std::optional<std::size_t> index = IndexOf(kLifetimeNames, *lifetime);
                    if (!index)
                    {
                        Error(at + ".lifetime", "A lifetime is held, fixed or impulse.");
                        return std::nullopt;
                    }
                    action.Lifetime = static_cast<AnimRequestLifetime>(*index);
                }
                if (const JsonValue* ticks = entry.Find("ticks"); ticks != nullptr && ticks->IsNumber())
                    action.FixedTicks = static_cast<std::uint32_t>(std::max(0.0, ticks->AsNumber()));
                if (const JsonValue* layers = entry.Find("layers"); layers != nullptr && layers->IsNumber())
                    action.Layers = static_cast<std::uint8_t>(
                        std::clamp(layers->AsNumber(), 0.0, 255.0));
                Named(entry.Find("params"), at + ".params", action.Params);
            }
            else if (const std::string* cancelled = text("cancel"))
            {
                action.Kind = AnimationScenarioActionKind::CancelRequest;
                action.Intent = *cancelled;
                if (const std::string* reason = text("reason"))
                {
                    const std::optional<std::size_t> index = IndexOf(kReasonNames, *reason);
                    if (!index || *index == 0)
                    {
                        Error(at + ".reason",
                              "A cancel reason is released, interrupted, failed or superseded.");
                        return std::nullopt;
                    }
                    action.Reason = static_cast<AnimCancelReason>(*index);
                }
            }
            else
            {
                Error(at, "An action sets or clears a fact, or issues or cancels a request.");
                return std::nullopt;
            }

            if (action.Kind == AnimationScenarioActionKind::IssueRequest
                || action.Kind == AnimationScenarioActionKind::CancelRequest)
            {
                const std::string* source = text("source");
                if (source == nullptr || source->empty())
                {
                    Error(at + ".source", "A request action names the participant it comes from.");
                    return std::nullopt;
                }
                action.Participant = *source;
            }

            for (const auto& [key, value] : entry.AsObject())
            {
                if (!IsKnown(key, { "tick", "set", "value", "clear", "issue", "cancel", "source",
                                    "lifetime", "ticks", "layers", "params", "reason" }))
                    action.Unknown.emplace_back(key, value);
            }
            return action;
        }
    };
}

AnimationScenarioValue AnimationScenarioValue::FromBool(bool value)
{
    AnimationScenarioValue out;
    out.Type = Kind::Bool;
    out.Bool = value;
    return out;
}

AnimationScenarioValue AnimationScenarioValue::FromNumber(double value)
{
    AnimationScenarioValue out;
    out.Type = Kind::Number;
    out.Number = value;
    return out;
}

AnimationScenarioValue AnimationScenarioValue::FromName(std::string value)
{
    AnimationScenarioValue out;
    out.Type = Kind::Name;
    out.Name = std::move(value);
    return out;
}

void AnimationScenario::Append(AnimationScenarioAction action)
{
    const auto at = std::upper_bound(
        Actions.begin(), Actions.end(), action.Tick,
        [](AnimTick tick, const AnimationScenarioAction& existing) { return tick < existing.Tick; });
    Actions.insert(at, std::move(action));
}

void AnimationScenario::TruncateAfter(AnimTick tick)
{
    std::erase_if(Actions, [tick](const AnimationScenarioAction& action) { return action.Tick > tick; });
}

bool AnimationScenario::HasParticipant(std::string_view name) const
{
    return std::find(Participants.begin(), Participants.end(), name) != Participants.end();
}

JsonValue WriteAnimationScenario(const AnimationScenario& scenario)
{
    JsonValue::Array actions;
    for (const AnimationScenarioAction& action : scenario.Actions)
    {
        JsonValue::Object entry;
        entry.emplace_back("tick", JsonValue(static_cast<double>(action.Tick)));
        switch (action.Kind)
        {
        case AnimationScenarioActionKind::SetFact:
            entry.emplace_back("set", JsonValue(action.Fact));
            entry.emplace_back("value", WriteValue(action.Value));
            break;
        case AnimationScenarioActionKind::ClearFact:
            entry.emplace_back("clear", JsonValue(action.Fact));
            break;
        case AnimationScenarioActionKind::IssueRequest:
            entry.emplace_back("issue", JsonValue(action.Intent));
            entry.emplace_back("source", JsonValue(action.Participant));
            entry.emplace_back("lifetime",
                               JsonValue(std::string(kLifetimeNames[static_cast<std::size_t>(action.Lifetime)])));
            if (action.Lifetime == AnimRequestLifetime::Fixed)
                entry.emplace_back("ticks", JsonValue(static_cast<double>(action.FixedTicks)));
            if (action.Layers != kAnimAllLayers)
                entry.emplace_back("layers", JsonValue(static_cast<double>(action.Layers)));
            if (!action.Params.empty())
                entry.emplace_back("params", WriteNamedValues(action.Params));
            break;
        case AnimationScenarioActionKind::CancelRequest:
            entry.emplace_back("cancel", JsonValue(action.Intent));
            entry.emplace_back("source", JsonValue(action.Participant));
            entry.emplace_back("reason",
                               JsonValue(std::string(kReasonNames[static_cast<std::size_t>(action.Reason)])));
            break;
        }
        for (const auto& unknown : action.Unknown)
            entry.push_back(unknown);
        actions.emplace_back(std::move(entry));
    }

    JsonValue::Array participants;
    for (const std::string& participant : scenario.Participants)
        participants.emplace_back(participant);

    JsonValue::Array declared;
    for (const std::string& tag : scenario.DeclaredTags)
        declared.emplace_back(tag);

    JsonValue::Object root;
    root.emplace_back("type", JsonValue(std::string(kAnimationScenarioType)));
    root.emplace_back("version", JsonValue(kAnimationScenarioVersion));
    root.emplace_back("name", JsonValue(scenario.Name));
    root.emplace_back("rig", JsonValue(scenario.RigPath));
    root.emplace_back("tick_rate", JsonValue(static_cast<double>(scenario.TickRate)));
    root.emplace_back("seed", JsonValue(static_cast<double>(scenario.Seed)));
    root.emplace_back("participants", JsonValue(std::move(participants)));
    if (!scenario.DeclaredTags.empty())
        root.emplace_back("declared_tags", JsonValue(std::move(declared)));
    if (scenario.Role == AnimationPreviewRole::Client)
        root.emplace_back("role", JsonValue("client"));
    if (!scenario.Recorders.empty())
    {
        JsonValue::Array recorders;
        for (const std::string& verb : scenario.Recorders)
            recorders.emplace_back(verb);
        root.emplace_back("recorders", JsonValue(std::move(recorders)));
    }
    root.emplace_back("inputs", WriteNamedValues(scenario.Inputs));
    root.emplace_back("actions", JsonValue(std::move(actions)));
    for (const auto& unknown : scenario.Unknown)
        root.push_back(unknown);
    return JsonValue(std::move(root));
}

std::optional<AnimationScenario> ReadAnimationScenario(const JsonValue& document,
                                                       std::string_view assetPath,
                                                       std::vector<AnimDiagnostic>& diagnostics)
{
    Reader reader{ assetPath, diagnostics };
    const JsonValue* type = document.Find("type");
    if (!document.IsObject() || type == nullptr || !type->IsString()
        || type->AsString() != kAnimationScenarioType)
    {
        reader.Error("$.type", std::format("This is not an {} document.", kAnimationScenarioType));
        return std::nullopt;
    }
    const JsonValue* version = document.Find("version");
    if (version == nullptr || !version->IsNumber()
        || static_cast<int>(version->AsNumber()) != kAnimationScenarioVersion)
    {
        reader.Error("$.version", std::format("This editor reads scenario version {}.",
                                              kAnimationScenarioVersion));
        return std::nullopt;
    }

    AnimationScenario scenario;
    if (const JsonValue* name = document.Find("name"); name != nullptr && name->IsString())
        scenario.Name = name->AsString();
    if (const JsonValue* rig = document.Find("rig"); rig != nullptr && rig->IsString())
        scenario.RigPath = rig->AsString();
    if (const JsonValue* rate = document.Find("tick_rate"); rate != nullptr)
    {
        if (!rate->IsNumber() || rate->AsNumber() < 1.0 || rate->AsNumber() > 1000.0)
            reader.Error("$.tick_rate", "A tick rate is between 1 and 1000 ticks per second.");
        else
            scenario.TickRate = static_cast<std::uint32_t>(rate->AsNumber());
    }
    if (const JsonValue* seed = document.Find("seed"); seed != nullptr && seed->IsNumber())
        scenario.Seed = static_cast<std::uint64_t>(seed->AsNumber());
    if (const JsonValue* participants = document.Find("participants");
        participants != nullptr && participants->IsArray())
    {
        for (std::size_t i = 0; i < participants->AsArray().size(); ++i)
        {
            const JsonValue& participant = participants->AsArray()[i];
            if (!participant.IsString() || participant.AsString().empty())
            {
                reader.Error(std::format("$.participants[{}]", i), "A participant is a name.");
                continue;
            }
            if (scenario.HasParticipant(participant.AsString()))
            {
                reader.Error(std::format("$.participants[{}]", i), "Participants have distinct names.");
                continue;
            }
            scenario.Participants.push_back(participant.AsString());
        }
    }
    if (const JsonValue* declared = document.Find("declared_tags");
        declared != nullptr && declared->IsArray())
    {
        for (std::size_t i = 0; i < declared->AsArray().size(); ++i)
        {
            const JsonValue& tag = declared->AsArray()[i];
            if (!tag.IsString() || tag.AsString().empty())
                reader.Error(std::format("$.declared_tags[{}]", i), "A declared tag is a tag name.");
            else
                scenario.DeclaredTags.push_back(tag.AsString());
        }
    }
    if (const JsonValue* role = document.Find("role"); role != nullptr)
    {
        if (role->IsString() && role->AsString() == "client")
            scenario.Role = AnimationPreviewRole::Client;
        else if (!role->IsString() || role->AsString() != "authority")
            reader.Error("$.role", "A role is \"authority\" or \"client\".");
    }
    if (const JsonValue* recorders = document.Find("recorders"); recorders != nullptr && recorders->IsArray())
    {
        for (std::size_t i = 0; i < recorders->AsArray().size(); ++i)
        {
            const JsonValue& verb = recorders->AsArray()[i];
            if (!verb.IsString() || verb.AsString().empty())
                reader.Error(std::format("$.recorders[{}]", i), "A recorder names a verb.");
            else
                scenario.Recorders.push_back(verb.AsString());
        }
    }
    reader.Named(document.Find("inputs"), "$.inputs", scenario.Inputs);

    if (const JsonValue* actions = document.Find("actions"); actions != nullptr && actions->IsArray())
    {
        AnimTick previous = 0;
        for (std::size_t i = 0; i < actions->AsArray().size(); ++i)
        {
            const std::string at = std::format("$.actions[{}]", i);
            std::optional<AnimationScenarioAction> action = reader.Action(actions->AsArray()[i], at);
            if (!action)
                continue;
            if (action->Tick < previous)
                reader.Error(at + ".tick", "Actions are listed in tick order.");
            previous = std::max(previous, action->Tick);
            if (!action->Participant.empty() && !scenario.HasParticipant(action->Participant))
                reader.Error(at + ".source",
                             std::format("'{}' is not one of this scenario's participants.",
                                         action->Participant));
            scenario.Append(std::move(*action));
        }
    }

    for (const auto& [key, value] : document.AsObject())
    {
        if (!IsKnown(key, { "type", "version", "name", "rig", "tick_rate", "seed", "participants",
                            "declared_tags", "role", "recorders", "inputs", "actions" }))
            scenario.Unknown.emplace_back(key, value);
    }
    return scenario;
}

bool SaveAnimationScenario(const AnimationScenario& scenario, const std::string& filePath,
                           std::string& error)
{
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = std::format("Could not open '{}' for writing.", filePath);
        return false;
    }
    out << JsonFormat(WriteAnimationScenario(scenario), 4) << '\n';
    if (!out)
    {
        error = std::format("Could not write '{}'.", filePath);
        return false;
    }
    return true;
}

std::optional<AnimationScenario> LoadAnimationScenario(const std::string& filePath,
                                                       std::vector<AnimDiagnostic>& diagnostics)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        diagnostics.push_back(AnimDiagnostic{ AnimDiagnosticSeverity::Error, "anim.scenario.unreadable",
                                              filePath, {}, "The scenario file could not be opened." });
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    JsonParseError parseError;
    const std::optional<JsonValue> document = JsonParse(text.str(), &parseError);
    if (!document)
    {
        diagnostics.push_back(AnimDiagnostic{ AnimDiagnosticSeverity::Error, "anim.scenario.unreadable",
                                              filePath, {},
                                              std::format("Not valid JSON: {}", parseError.Message) });
        return std::nullopt;
    }
    return ReadAnimationScenario(*document, filePath, diagnostics);
}
