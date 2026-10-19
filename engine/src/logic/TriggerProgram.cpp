#include <algorithm>
#include <authored/VerbBindingData.h>
#include <cmath>
#include <format>
#include <logic/TriggerProgram.h>

namespace
{
struct ContextInput
{
    std::string_view Name;
    TriggerInput Input;
    AuthoredValue Example;
};
const auto &ContextInputs()
{
    static const std::array<ContextInput, 7> inputs = {{
        {"trigger", TriggerInput::Trigger, AuthoredValue::Entity({1, 1})},
        {"activator", TriggerInput::Activator, AuthoredValue::Entity({1, 1})},
        {"event", TriggerInput::Event, AuthoredValue::Enum("Enter")},
        {"tick", TriggerInput::Tick, AuthoredValue::Int(1)},
        {"trigger_up", TriggerInput::Up, AuthoredValue::Vector({{0, 1, 0, 0}, 3})},
        {"trigger_forward", TriggerInput::Forward, AuthoredValue::Vector({{0, 0, -1, 0}, 3})},
        {"trigger_right", TriggerInput::Right, AuthoredValue::Vector({{1, 0, 0, 0}, 3})},
    }};
    return inputs;
}
bool ValidateVersion(const JsonValue &source, std::vector<std::string> &errors)
{
    const auto *version = source.Find("version");
    if (!source.IsObject() || version == nullptr || !version->IsNumber() || version->AsNumber() != 1)
    {
        errors.emplace_back("trigger program version: expected version 1");
        return false;
    }
    return true;
}
template <typename Desc>
bool ParseArguments(const JsonValue &call, Desc &desc, std::vector<AssetRef> &dependencies,
                    std::vector<std::string> &errors)
{
    const auto *arguments = call.Find("arguments");
    if (arguments == nullptr)
        return true;
    if (!arguments->IsObject())
    {
        errors.push_back(desc.Key + " arguments: expected an object");
        return false;
    }
    bool ok = true;
    for (const auto &[key, source] : arguments->AsObject())
    {
        VerbBindingArgument argument;
        std::string error;
        if (!ParseVerbBindingArgument(desc.Key, key, source, argument, dependencies, error))
        {
            errors.push_back(std::move(error));
            ok = false;
            continue;
        }
        if (argument.Source == VerbArgumentSource::Input &&
            std::ranges::find(desc.Inputs, argument.Text) == desc.Inputs.end())
            desc.Inputs.push_back(argument.Text);
        desc.Arguments.push_back(std::move(argument));
    }
    return ok;
}
bool BindInputs(std::string_view key, std::span<const VerbCompiledInput> inputs, std::vector<TriggerInput> &out,
                std::vector<std::string> &errors)
{
    bool ok = true;
    for (const auto &input : inputs)
    {
        const auto found = std::ranges::find(ContextInputs(), input.Name, &ContextInput::Name);
        if (found == ContextInputs().end())
        {
            errors.push_back(std::format("{} input '{}': unknown trigger context", key, input.Name));
            ok = false;
            continue;
        }
        for (const auto &destination : input.Destinations)
        {
            if (!AuthoredValueSatisfiesField(found->Example, destination.Expected))
            {
                errors.push_back(std::format("{} argument '{}': context '{}' has the wrong type", key,
                                             destination.Expected.Key, input.Name));
                ok = false;
            }
            if (found->Input == TriggerInput::Event)
                for (const char *event : {"Stay", "Exit"})
                    if (!AuthoredValueSatisfiesField(AuthoredValue::Enum(event), destination.Expected))
                    {
                        errors.push_back(std::format("{} argument '{}': event must accept Enter, Stay and Exit", key,
                                                     destination.Expected.Key));
                        ok = false;
                    }
        }
        out.push_back(found->Input);
    }
    return ok;
}
template <typename Compile>
bool Calls(const JsonValue &source, std::string_view field, std::size_t limit, Compile compile,
           std::vector<std::string> &errors)
{
    const auto *calls = source.Find(field);
    if (calls == nullptr)
        return true;
    if (!calls->IsArray() || calls->Size() > limit)
    {
        errors.push_back(std::format("trigger {}: expected at most {} rows", field, limit));
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < calls->Size(); ++i)
    {
        const auto &call = calls->AsArray()[i];
        const std::string key = std::format("trigger {}[{}]", field, i);
        const auto *enabled = call.Find("enabled");
        if (!call.IsObject() || (enabled != nullptr && !enabled->IsBool()))
        {
            errors.push_back(key + ": expected an object with a boolean enabled setting");
            ok = false;
        }
        else if (enabled == nullptr || enabled->AsBool())
            ok = compile(call, key) && ok;
    }
    return ok;
}
} // namespace

JsonValue EmptyTriggerProgram()
{
    return JsonValue::Object{{"version", 1},
                             {"require_components", JsonValue::Array{}},
                             {"conditions", JsonValue::Array{}},
                             {"commands", JsonValue::Array{}}};
}

bool ValidateTriggerActivation(const Trigger &trigger, std::vector<std::string> &errors)
{
    const auto before = errors.size();
    if (trigger.Event != TriggerEvent::Enter && trigger.Event != TriggerEvent::Stay &&
        trigger.Event != TriggerEvent::Exit)
        errors.emplace_back("trigger event: select Enter, Stay or Exit");
    if (!std::isfinite(trigger.Cooldown) || trigger.Cooldown < 0)
        errors.emplace_back("trigger cooldown: expected finite nonnegative seconds");
    if (!std::isfinite(trigger.StayInterval) || trigger.StayInterval <= 0)
        errors.emplace_back("trigger stay_interval: expected finite positive seconds");
    return errors.size() == before;
}

bool CollectTriggerProgramDependencies(const JsonValue &source, std::vector<AssetRef> &dependencies,
                                      std::vector<std::string> &errors)
{
    if (!ValidateVersion(source, errors))
        return false;
    const auto collect = [&](const JsonValue &call, const std::string &key)
    {
        VerbBindingDesc desc;
        desc.Key = key;
        return ParseArguments(call, desc, dependencies, errors);
    };
    const bool commands = Calls(source, "commands", kTriggerCommandLimit, collect, errors);
    const bool conditions = Calls(source, "conditions", kTriggerConditionLimit, collect, errors);
    return commands && conditions;
}

bool CompileTriggerProgram(const JsonValue &source, const VerbBindingEnvironment &environment,
                           const AuthoredQueryRegistry *queries, CompiledTriggerProgram &out,
                           std::vector<std::string> &errors)
{
    if (!ValidateVersion(source, errors))
        return false;
    CompiledTriggerProgram result;
    bool ok = true;
    if (const auto *required = source.Find("require_components"))
    {
        if (!required->IsArray() || required->Size() > kTriggerConditionLimit)
        {
            errors.emplace_back("trigger require_components: expected at most 32 component identities");
            ok = false;
        }
        else
            for (const auto &value : required->AsArray())
            {
                if (!value.IsString() || value.AsString().empty())
                {
                    errors.emplace_back("trigger require_components: expected a nonempty component identity");
                    ok = false;
                }
                else
                    result.RequiredComponents.push_back(MakeComponentTypeId(value.AsString()));
            }
    }
    ok = Calls(
             source, "commands", kTriggerCommandLimit,
             [&](const JsonValue &call, const std::string &key) {
                 const auto *verb = call.Find("verb");
                 if (verb == nullptr || !verb->IsString())
                 {
                     errors.push_back(key + " verb: select a Verb");
                     return false;
                 }
                 VerbBindingDesc desc;
                 desc.Key = key;
                 desc.VerbName = verb->AsString();
                 TriggerCommand command;
                 if (!ParseArguments(call, desc, result.Dependencies, errors) ||
                     !CompileVerbBinding(desc, environment, command.Binding, errors))
                     return false;
                 if (environment.Verbs->Get(command.Binding.Verb)->Kind != VerbKind::Instant)
                 {
                     errors.push_back(key +
                                      ": a lease requires an explicit lifetime owner; commands require instant Verbs");
                     return false;
                 }
                 if (!BindInputs(key, command.Binding.Inputs, command.Inputs, errors))
                     return false;
                 result.Commands.push_back(std::move(command));
                 return true;
             },
             errors) &&
         ok;
    ok = Calls(
             source, "conditions", kTriggerConditionLimit,
             [&](const JsonValue &call, const std::string &key) {
                 const auto *query = call.Find("query");
                 if (query == nullptr || !query->IsString() || queries == nullptr)
                 {
                     errors.push_back(key + " query: select a registered Query");
                     return false;
                 }
                 AuthoredQueryBindingDesc desc;
                 desc.Key = key;
                 desc.QueryName = query->AsString();
                 TriggerCondition condition;
                 if (!ParseArguments(call, desc, result.Dependencies, errors) ||
                     !CompileAuthoredQueryBinding(desc, *queries, environment, condition.Binding, errors))
                     return false;
                 if (queries->Get(condition.Binding.Query.Slot)->Result.Kind != DataFieldKind::Bool)
                 {
                     errors.push_back(key + " query: conditions must return boolean answers");
                     return false;
                 }
                 if (!BindInputs(key, condition.Binding.Inputs, condition.Inputs, errors))
                     return false;
                 result.Conditions.push_back(std::move(condition));
                 return true;
             },
             errors) &&
         ok;
    if (!ok)
        return false;
    out = std::move(result);
    return true;
}
