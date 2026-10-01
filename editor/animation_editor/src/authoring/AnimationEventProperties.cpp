#include "authoring/AnimationEventProperties.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigDocumentEdits.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRigData.h>
#include <authored/VerbBindingData.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace
{
constexpr std::array<std::string_view, 2> kScopes{ "Cosmetic", "Gameplay" };
constexpr std::string_view kInputKey = "inputs.";

std::string_view KindName(DataFieldKind kind)
{
    switch (kind)
    {
    case DataFieldKind::Bool:         return "bool";
    case DataFieldKind::Int:          return "int";
    case DataFieldKind::Float:        return "float";
    case DataFieldKind::String:       return "string";
    case DataFieldKind::Enum:         return "choice";
    case DataFieldKind::Vector:       return "vector";
    case DataFieldKind::Record:       return "record";
    case DataFieldKind::Array:        return "array";
    case DataFieldKind::Optional:     return "optional";
    case DataFieldKind::AssetRef:     return "asset";
    case DataFieldKind::DataAssetRef: return "data asset";
    case DataFieldKind::GameplayTag:  return "tag";
    case DataFieldKind::Entity:       return "entity";
    }
    return "?";
}

DataFieldKind InputKind(const VerbCompiledInput& input)
{
    return input.Destinations.empty() ? DataFieldKind::Float : input.Destinations.front().Expected.Kind;
}

VerbBindingArgument DefaultInput(const std::string& name, DataFieldKind kind)
{
    VerbBindingArgument input;
    input.Key = name;
    switch (kind)
    {
    case DataFieldKind::GameplayTag: input.Source = VerbArgumentSource::Tag; break;
    case DataFieldKind::Bool:        input.Literal = JsonValue(false); break;
    case DataFieldKind::String:
    case DataFieldKind::Enum:        input.Literal = JsonValue(std::string()); break;
    default:                         input.Literal = JsonValue(0.0); break;
    }
    return input;
}

const AnimRigData* RigData(const AnimationPreviewWorkspace& workspace)
{
    const DataAssetCache& data = workspace.DataCache();
    return data.TryGet<AnimRigData>(data.Find(workspace.Rig.Path), kAnimRigType);
}

// The bindings file of the rig's that declares `key`, or empty.
std::string BindingFileOf(const AnimationPreviewWorkspace& workspace, const std::string& key)
{
    const AnimRigData* rig = RigData(workspace);
    if (rig == nullptr)
        return {};
    const DataAssetCache& data = workspace.DataCache();
    for (const std::string& path : rig->BindingSetPaths)
        if (const VerbBindingLibrary* library = data.TryGet<VerbBindingLibrary>(data.Find(path), kVerbBindingsTypeName))
            if (library->Find(std::string_view(key)) != nullptr)
                return path;
    return {};
}

std::vector<std::string> BindingFiles(const AnimationPreviewWorkspace& workspace)
{
    const AnimRigData* rig = RigData(workspace);
    return rig != nullptr ? rig->BindingSetPaths : std::vector<std::string>{};
}

// An input's value as the card shows and takes it.
PropertyNode InputNode(const std::string& key, const std::string& label, DataFieldKind kind,
                       const VerbBindingArgument& value, const GameplayTagRegistry* tags)
{
    switch (kind)
    {
    case DataFieldKind::GameplayTag:
    {
        std::vector<std::string_view> names;
        std::size_t current = 0;
        if (tags != nullptr)
            for (std::size_t id = 1; id <= tags->Size(); ++id)
                if (const std::string_view name = tags->GetName(GameplayTagId{ static_cast<std::uint32_t>(id) });
                    !name.empty())
                {
                    if (name == value.Text)
                        current = names.size();
                    names.push_back(name);
                }
        if (names.empty() || (!value.Text.empty() && names[current] != value.Text))
        {
            names.insert(names.begin(), value.Text);
            current = 0;
        }
        return ChoiceProperty(key, label, names, current);
    }
    case DataFieldKind::Bool: return ToggleProperty(key, label, value.Literal.IsBool() && value.Literal.AsBool());
    case DataFieldKind::Int:
    case DataFieldKind::Float:
        return NumberProperty(key, label, value.Literal.IsNumber() ? value.Literal.AsNumber() : 0.0, 0.0, -1.0,
                              kind == DataFieldKind::Int ? 1.0 : 0.0);
    case DataFieldKind::String:
    case DataFieldKind::Enum:
        return TextProperty(key, label, value.Literal.IsString() ? value.Literal.AsString() : std::string());
    default:
        // Vectors, records and arrays take their JSON text.
        return TextProperty(key, label, JsonStringify(value.Literal), "As JSON");
    }
}

bool SetInput(VerbBindingArgument& value, DataFieldKind kind, std::string_view text)
{
    if (kind == DataFieldKind::GameplayTag)
    {
        value.Source = VerbArgumentSource::Tag;
        value.Text = text;
        return true;
    }
    value.Source = VerbArgumentSource::Literal;
    switch (kind)
    {
    case DataFieldKind::Bool:
        if (const std::optional<bool> on = ParseToggle(text))
        {
            value.Literal = JsonValue(*on);
            return true;
        }
        return false;
    case DataFieldKind::Int:
    case DataFieldKind::Float:
        if (const std::optional<double> number = ParseNumber(text))
        {
            value.Literal = JsonValue(kind == DataFieldKind::Int ? std::round(*number) : *number);
            return true;
        }
        return false;
    case DataFieldKind::String:
    case DataFieldKind::Enum:
        value.Literal = JsonValue(std::string(text));
        return true;
    default:
        if (std::optional<JsonValue> parsed = JsonParse(std::string(text)))
        {
            value.Literal = std::move(*parsed);
            return true;
        }
        return false;
    }
}
} // namespace

std::string EventLabel(const AnimationClipEvent& event)
{
    return event.Name.empty() ? std::format("Event {}", event.Key) : event.Name;
}

void PreviewEventTime(AnimationClipEventsSet& events, AnimationClipEventsDocument& document, std::uint32_t key,
                      double time)
{
    const AnimationClipEvent* event = document.Find(key);
    if (event == nullptr)
        return;
    if (!document.IsEditing())
        document.BeginEdit(key);
    AnimationClipEvent moved = *event;
    moved.Time = static_cast<float>(std::clamp(time, 0.0, 1.0));
    document.PreviewEdit(std::move(moved));
    events.Changed(document);
}

AnimationClipEventsDocument* AnimationEventProperties::Document() const
{
    return Workspace.ClipEvents.Active();
}

const AnimationClipEvent* AnimationEventProperties::Event() const
{
    AnimationClipEventsDocument* document = Document();
    const std::optional<std::uint32_t> picked = Picked();
    return document != nullptr && picked ? document->Find(*picked) : nullptr;
}

std::vector<PropertyNode> AnimationEventProperties::Nodes() const
{
    AnimationClipEventsDocument* document = Document();
    if (document == nullptr)
        return {};
    std::vector<PropertyNode> nodes;
    const AnimationClipEvent* event = Event();
    if (event == nullptr)
    {
        nodes.push_back(NoteProperty("clip", "Clip", document->ClipPath()));
        nodes.push_back(NoteProperty("sidecar", "Authored in", document->SidecarPath().filename().string()));
        nodes.push_back(NoteProperty("count", "Events", std::to_string(document->Events().size())));
    }
    else
    {
        nodes.push_back(TextProperty("name", "Name", event->Name));
        nodes.push_back(NumberProperty("time", "Time", event->Time, 0.0, 1.0, 0.001, "Along the clip, 0 to 1"));
        nodes.push_back(ChoiceProperty("scope", "Scope", kScopes, event->Scope == AnimEventScope::Gameplay ? 1 : 0));
        if (event->Scope == AnimEventScope::Cosmetic)
        {
            nodes.push_back(ToggleProperty("own_weight", "Own threshold", event->MinWeight.has_value(),
                                           "Below the behavior's event weight, the event is suppressed"));
            if (event->MinWeight)
                nodes.push_back(NumberProperty("weight", "Weight", *event->MinWeight, 0.0, 1.0, 0.01));
        }
        else
            nodes.push_back(
                NoteProperty("authority", "Raised by", "The simulation authority only; a client never raises it."));
        const std::vector<PropertyNode> binding = BindingNodes(*event);
        nodes.insert(nodes.end(), binding.begin(), binding.end());
    }
    for (std::size_t i = 0; const std::string& problem : document->Problems())
        nodes.push_back(NoteProperty(std::format("problems.{}", i++), "Problem", problem));
    return nodes;
}

std::vector<PropertyNode> AnimationEventProperties::BindingNodes(const AnimationClipEvent& event) const
{
    std::vector<PropertyNode> nodes;
    const AnimationPreviewSession& simulation = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = simulation.Rig();
    const VerbRegistry* verbs = simulation.Verbs();

    std::vector<std::string_view> keys;
    std::size_t current = 0;
    std::vector<AnimationBindingView> views;
    if (rig != nullptr && verbs != nullptr)
        views = DescribeAnimationBindings(*rig, *verbs);
    for (const AnimationBindingView& view : views)
    {
        if (view.Key == event.Binding)
            current = keys.size();
        keys.push_back(view.Key);
    }
    if (keys.empty() || keys[current] != event.Binding)
    {
        keys.insert(keys.begin(), event.Binding);
        current = 0;
    }
    PropertyNode binding = ChoiceProperty("binding", "Binding", keys, current, "The rig binding this event raises");
    if (!BindingFileOf(Workspace, event.Binding).empty())
        binding.Descriptor.Verbs = { { "open", "Open Binding" } };
    nodes.push_back(std::move(binding));

    const CompiledVerbBinding* compiled = rig != nullptr ? rig->Bindings.Find(event.Binding) : nullptr;
    const VerbDefinition* definition = compiled != nullptr && verbs != nullptr ? verbs->Get(compiled->Verb) : nullptr;
    if (compiled == nullptr)
        nodes.push_back(NoteProperty("contract", "Contract",
                                     std::format("No binding '{}' in the rig's bindings.", event.Binding)));
    else if (definition != nullptr)
    {
        std::string arguments;
        for (const DataFieldSchema& argument : definition->Arguments.Children)
            arguments += std::format("{}{} {}", arguments.empty() ? "" : ", ", argument.Key, KindName(argument.Kind));
        nodes.push_back(NoteProperty("contract", "Contract", std::format("{}({})", definition->Name, arguments)));
    }

    if (compiled != nullptr)
    {
        const VerbBindingEnvironment environment{ .Verbs = verbs, .Tags = simulation.Tags() };
        const std::vector<AnimationEventInputCheck> checks = CheckAnimationEventInputs(event, *compiled, environment);
        PropertyNode inputs = GroupProperty("inputs", "Inputs");
        for (const VerbCompiledInput& input : compiled->Inputs)
        {
            const std::string key = std::string(kInputKey) + input.Name;
            const auto supplied = std::ranges::find(event.Inputs, input.Name, &VerbBindingArgument::Key);
            if (supplied == event.Inputs.end())
            {
                PropertyNode missing = NoteProperty(key, input.Name, "Not supplied");
                missing.Descriptor.Verbs = { { "supply", "Supply" } };
                inputs.Children.push_back(std::move(missing));
                continue;
            }
            PropertyNode value = InputNode(key, input.Name, InputKind(input), *supplied, simulation.Tags());
            std::string destinations;
            for (const VerbInputDestination& destination : input.Destinations)
            {
                const std::string argument = definition != nullptr
                                                     && destination.ArgumentSlot < definition->Arguments.Children.size()
                                                 ? definition->Arguments.Children[destination.ArgumentSlot].Key
                                                 : std::string("?");
                destinations += std::format("{}{}", destinations.empty() ? "" : ", ", argument);
            }
            value.Descriptor.Tooltip = "Fills " + destinations;
            inputs.Children.push_back(std::move(value));
            if (const auto check = std::ranges::find(checks, input.Name, &AnimationEventInputCheck::Input);
                check != checks.end() && !check->Valid)
                inputs.Children.push_back(NoteProperty(key + "~problem", "Refused", check->Message));
        }
        for (const AnimationEventInputCheck& check : checks)
            if (!check.Valid && check.Supplied
                && std::ranges::none_of(compiled->Inputs,
                                        [&](const VerbCompiledInput& in) { return in.Name == check.Input; }))
            {
                PropertyNode extra = NoteProperty(std::string(kInputKey) + check.Input, check.Input, check.Message);
                extra.Descriptor.Verbs = { { "remove", "Remove" } };
                inputs.Children.push_back(std::move(extra));
            }
        nodes.push_back(std::move(inputs));
    }

    // A binding to raise instead, made in one of the rig's bindings files.
    PropertyNode create = GroupProperty("new_binding", "New binding");
    create.Descriptor.Advanced = true;
    const std::vector<std::string> files = BindingFiles(Workspace);
    if (files.empty())
        create.Children.push_back(NoteProperty("new_binding.none", "Files", "The rig lists no bindings files."));
    else
    {
        std::vector<std::string_view> fileNames(files.begin(), files.end());
        const auto file = std::ranges::find(files, NewFile);
        create.Children.push_back(ChoiceProperty("new_binding.file", "In", fileNames,
                                                 file != files.end() ? static_cast<std::size_t>(file - files.begin()) : 0));
        std::vector<std::string> verbNames;
        if (verbs != nullptr)
            for (const VerbId id : verbs->Live())
                if (const VerbDefinition* candidate = verbs->Get(id))
                    verbNames.push_back(candidate->Name);
        std::vector<std::string_view> verbViews(verbNames.begin(), verbNames.end());
        if (verbViews.empty() || std::ranges::find(verbViews, NewVerb) == verbViews.end())
            verbViews.insert(verbViews.begin(), NewVerb);
        create.Children.push_back(ChoiceProperty(
            "new_binding.verb", "Verb", verbViews,
            static_cast<std::size_t>(std::ranges::find(verbViews, NewVerb) - verbViews.begin())));
        PropertyNode key = TextProperty("new_binding.key", "Key", NewKey);
        if (!NewVerb.empty() && !NewKey.empty())
            key.Descriptor.Verbs = { { "create", "Create Binding" } };
        create.Children.push_back(std::move(key));
    }
    nodes.push_back(std::move(create));
    return nodes;
}

bool AnimationEventProperties::BeginEdit(std::string_view key)
{
    AnimationClipEventsDocument* document = Document();
    const AnimationClipEvent* event = Event();
    if (document == nullptr || (event == nullptr && !key.starts_with("new_binding.")))
        return false;
    if (key == "time")
        document->BeginEdit(event->Key);
    return true;
}

PropertyEditResult AnimationEventProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    AnimationClipEventsDocument* document = Document();
    const AnimationClipEvent* event = Event();
    if (key != "time" || document == nullptr || event == nullptr)
        return PropertyEditResult::Unchanged;
    const std::optional<double> time = ParseNumber(value, 0.0, 1.0);
    if (!time)
        return PropertyEditResult::Refused;
    PreviewEventTime(Workspace.ClipEvents, *document, event->Key, *time);
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationEventProperties::CommitEdit(std::string_view key, std::string_view value)
{
    if (key == "time")
    {
        const PropertyEditResult result = PreviewEdit(key, value);
        if (AnimationClipEventsDocument* document = Document(); document != nullptr && document->IsEditing())
        {
            document->CommitEdit();
            Workspace.ClipEvents.Changed(*document);
        }
        return result;
    }
    if (key == "new_binding.file")
        NewFile = value;
    else if (key == "new_binding.verb")
        NewVerb = value;
    else if (key == "new_binding.key")
        NewKey = value;
    if (key.starts_with("new_binding."))
        return PropertyEditResult::Applied;

    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    return Replace([&](AnimationClipEvent& event) {
        if (key == "name")
            event.Name = value;
        else if (key == "scope")
        {
            const std::optional<std::size_t> scope = ParseChoice(value, kScopes);
            if (!scope)
                return false;
            event.Scope = *scope == 1 ? AnimEventScope::Gameplay : AnimEventScope::Cosmetic;
            if (event.Scope == AnimEventScope::Gameplay)
                event.MinWeight.reset();
        }
        else if (key == "own_weight")
        {
            const std::optional<bool> own = ParseToggle(value);
            if (!own)
                return false;
            event.MinWeight = *own ? std::optional(event.MinWeight.value_or(0.5f)) : std::nullopt;
        }
        else if (key == "weight")
        {
            const std::optional<double> weight = ParseNumber(value, 0.0, 1.0);
            if (!weight)
                return false;
            event.MinWeight = static_cast<float>(*weight);
        }
        else if (key == "binding")
        {
            event.Binding = value;
            // Keep what already fits the new binding; start the rest from defaults.
            const CompiledVerbBinding* compiled = rig != nullptr ? rig->Bindings.Find(event.Binding) : nullptr;
            std::vector<VerbBindingArgument> inputs;
            if (compiled != nullptr)
                for (const VerbCompiledInput& input : compiled->Inputs)
                {
                    const auto kept = std::ranges::find(event.Inputs, input.Name, &VerbBindingArgument::Key);
                    inputs.push_back(kept != event.Inputs.end() ? *kept : DefaultInput(input.Name, InputKind(input)));
                }
            event.Inputs = std::move(inputs);
        }
        else if (key.starts_with(kInputKey))
        {
            const std::string name(key.substr(kInputKey.size()));
            const CompiledVerbBinding* compiled = rig != nullptr ? rig->Bindings.Find(event.Binding) : nullptr;
            const auto input = compiled != nullptr ? std::ranges::find(compiled->Inputs, name, &VerbCompiledInput::Name)
                                                   : std::vector<VerbCompiledInput>::const_iterator{};
            const auto supplied = std::ranges::find(event.Inputs, name, &VerbBindingArgument::Key);
            if (compiled == nullptr || input == compiled->Inputs.end() || supplied == event.Inputs.end())
                return false;
            return SetInput(*supplied, InputKind(*input), value);
        }
        else
            return false;
        return true;
    });
}

void AnimationEventProperties::CancelEdit(std::string_view key)
{
    AnimationClipEventsDocument* document = Document();
    if (key == "time" && document != nullptr && document->IsEditing())
    {
        document->CancelEdit();
        Workspace.ClipEvents.Changed(*document);
    }
}

PropertyEditResult AnimationEventProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const AnimationClipEvent* event = Event();
    if (event == nullptr)
        return PropertyEditResult::Gone;
    if (key == "binding" && verb == "open")
    {
        const std::string file = BindingFileOf(Workspace, event->Binding);
        return !file.empty() && Workspace.Documents.OpenOrFocus(file, Workspace.DocumentError) != nullptr
                   ? PropertyEditResult::Applied
                   : PropertyEditResult::Refused;
    }
    if (key == "new_binding.key" && verb == "create")
    {
        const std::vector<std::string> files = BindingFiles(Workspace);
        const std::string file = NewFile.empty() && !files.empty() ? files.front() : NewFile;
        if (!CreateAnimationBinding(Workspace.Documents, Workspace.Rig.Simulation, file, NewKey, NewVerb,
                                    Workspace.DocumentError))
            return PropertyEditResult::Refused;
        Workspace.Rig.Simulation.Rebind();
        const std::string created = NewKey;
        NewKey.clear();
        return Replace([&](AnimationClipEvent& edited) {
            edited.Binding = created;
            edited.Inputs.clear();
            return true;
        });
    }
    if (!key.starts_with(kInputKey))
        return PropertyEditResult::Gone;
    const std::string name(key.substr(kInputKey.size()));
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    return Replace([&](AnimationClipEvent& edited) {
        if (verb == "remove")
            return std::erase_if(edited.Inputs, [&](const VerbBindingArgument& input) { return input.Key == name; }) > 0;
        if (verb != "supply")
            return false;
        const CompiledVerbBinding* compiled = rig != nullptr ? rig->Bindings.Find(edited.Binding) : nullptr;
        if (compiled == nullptr)
            return false;
        const auto input = std::ranges::find(compiled->Inputs, name, &VerbCompiledInput::Name);
        if (input == compiled->Inputs.end())
            return false;
        edited.Inputs.push_back(DefaultInput(name, InputKind(*input)));
        return true;
    });
}

PropertyEditResult AnimationEventProperties::Replace(const std::function<bool(AnimationClipEvent&)>& edit)
{
    AnimationClipEventsDocument* document = Document();
    const AnimationClipEvent* event = Event();
    if (document == nullptr || event == nullptr)
        return PropertyEditResult::Gone;
    AnimationClipEvent edited = *event;
    if (!edit(edited))
        return PropertyEditResult::Refused;
    document->Replace(std::move(edited));
    Workspace.ClipEvents.Changed(*document);
    return PropertyEditResult::Applied;
}
