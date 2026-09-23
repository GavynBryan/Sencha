#include "authoring/AnimationEventBindings.h"

#include <algorithm>
#include <format>

std::vector<AnimationBindingView> DescribeAnimationBindings(const AnimBoundRig& rig, const VerbRegistry& verbs)
{
    std::vector<AnimationBindingView> views;
    for (const CompiledVerbBinding& binding : rig.Bindings.All())
    {
        const VerbDefinition* definition = verbs.Get(binding.Verb);
        AnimationBindingView view;
        view.Key = binding.KeyText;
        view.Verb = definition != nullptr ? definition->Name : std::string("(retired verb)");
        for (const VerbCompiledInput& input : binding.Inputs)
        {
            AnimationBindingInputView inputView;
            inputView.Name = input.Name;
            for (const VerbInputDestination& destination : input.Destinations)
            {
                const std::string argument = definition != nullptr
                        && destination.ArgumentSlot < definition->Arguments.Children.size()
                    ? definition->Arguments.Children[destination.ArgumentSlot].Key
                    : std::format("#{}", destination.ArgumentSlot);
                inputView.Destinations.emplace_back(argument, destination.Expected.Kind);
            }
            view.Inputs.push_back(std::move(inputView));
        }
        views.push_back(std::move(view));
    }
    return views;
}

std::vector<AnimationEventInputCheck> CheckAnimationEventInputs(const AnimationClipEvent& event,
                                                                const CompiledVerbBinding& binding,
                                                                const VerbBindingEnvironment& environment)
{
    std::vector<AnimationEventInputCheck> checks;
    for (const VerbCompiledInput& input : binding.Inputs)
    {
        AnimationEventInputCheck check;
        check.Input = input.Name;
        const auto supplied = std::ranges::find(event.Inputs, input.Name, &VerbBindingArgument::Key);
        check.Supplied = supplied != event.Inputs.end();
        if (!check.Supplied)
        {
            check.Message = "The binding takes this input and the event supplies none.";
            checks.push_back(std::move(check));
            continue;
        }
        VerbValue value;
        std::vector<std::string> errors;
        check.Valid = CompileVerbInputValue(*supplied, input, environment, binding.KeyText, value, errors);
        if (!check.Valid && !errors.empty())
            check.Message = errors.front();
        checks.push_back(std::move(check));
    }
    for (const VerbBindingArgument& supplied : event.Inputs)
    {
        const bool taken = std::ranges::any_of(binding.Inputs, [&supplied](const VerbCompiledInput& input) {
            return input.Name == supplied.Key;
        });
        if (!taken)
            checks.push_back(AnimationEventInputCheck{ supplied.Key, true, false,
                                                       "The binding takes no input by this name." });
    }
    return checks;
}

JsonValue MakeAnimationBindingRecord(std::string_view key, const VerbDefinition& verb)
{
    JsonValue::Array inputs;
    JsonValue::Object arguments;
    for (const DataFieldSchema& argument : verb.Arguments.Children)
    {
        inputs.emplace_back(argument.Key);
        JsonValue::Object source;
        source.emplace_back("input", JsonValue(argument.Key));
        arguments.emplace_back(argument.Key, JsonValue(std::move(source)));
    }
    JsonValue::Object record;
    record.emplace_back("key", JsonValue(std::string(key)));
    record.emplace_back("verb", JsonValue(verb.Name));
    record.emplace_back("inputs", JsonValue(std::move(inputs)));
    record.emplace_back("arguments", JsonValue(std::move(arguments)));
    return JsonValue(std::move(record));
}

bool AddAnimationBindingRecord(JsonValue& root, JsonValue record)
{
    JsonValue* data = root.Find("data");
    const JsonValue* key = record.Find("key");
    if (data == nullptr || !data->IsObject() || key == nullptr || !key->IsString())
        return false;
    JsonValue* bindings = data->Find("bindings");
    if (bindings == nullptr)
    {
        data->AsObject().emplace_back("bindings", JsonValue(JsonValue::Array{}));
        bindings = data->Find("bindings");
    }
    if (!bindings->IsArray())
        return false;
    for (const JsonValue& existing : bindings->AsArray())
    {
        const JsonValue* existingKey = existing.Find("key");
        if (existingKey != nullptr && existingKey->IsString() && existingKey->AsString() == key->AsString())
            return false;
    }
    bindings->AsArray().push_back(std::move(record));
    return true;
}
