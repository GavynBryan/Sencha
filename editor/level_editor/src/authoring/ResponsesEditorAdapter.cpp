#include "authoring/ResponsesEditorAdapter.h"

#include "authoring/AffordanceStrokes.h"
#include "commands/CommandStack.h"
#include "data/JsonObjectEdit.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"
#include "vocabulary/AuthoredConstantText.h"
#include "vocabulary/VerbSequenceRows.h"

#include <authored/AuthoredEventRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchema.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <physics/components/Collider.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view kBindingPrefix = "b.";
constexpr std::string_view kSequencePrefix = "s.";
constexpr std::array<std::string_view, 2> kScopes = { "Binding", "Instigator" };

template <typename T>
bool Carries(const World& world, EntityId entity)
{
    return world.IsRegistered<T>() && world.HasComponent<T>(entity);
}

// A volume brush is a level trigger whatever it senses; anything else is one
// only if it carries a sensor collider. Everything else answers as behaviors.
bool IsLevelTrigger(const World& world, const EditorScene& scene, EntityId entity)
{
    if (scene.TryGetBrush(entity) != nullptr)
        return Carries<BrushVolume>(world, entity);
    const Collider* collider = world.IsRegistered<Collider>() ? world.TryGet<Collider>(entity) : nullptr;
    return collider != nullptr && collider->IsTrigger;
}

bool IsSensorEvent(std::string_view name)
{
    return name.starts_with("sensor.");
}

// Why nothing would deliver to this trigger, if nothing would.
std::string UnsensedReason(const World& world, const EditorScene& scene, EntityId entity)
{
    if (scene.TryGetBrush(entity) != nullptr && !Carries<VolumeSensor>(world, entity))
        return "This volume does not cook to a sensor: add a volume sensor, or nothing enters it.";
    return {};
}

// The bindings as authored. A level trigger with none offers the first one a
// trigger usually wants, written only once something in it is edited.
JsonValue BindingsOf(const World& world, EntityId entity, bool levelTrigger)
{
    const JsonValue* source = ResponseBindingsSource(world, entity);
    if (source != nullptr && source->IsObject() && !source->AsObject().empty())
        return *source;
    if (!levelTrigger)
        return EmptyResponseBindings();
    return JsonValue(JsonValue::Object{
        { "enter", JsonValue(JsonValue::Object{ { "on", JsonValue(std::string(kSensorEnterEvent)) },
                                                { "sequence", EmptyVerbSequence() } }) } });
}

// The events a binding here may answer: a level trigger's sensor events first,
// and sensor events only where something senses.
std::vector<std::string> EventChoices(const World& world, const EditorScene& scene, EntityId entity,
                                      std::vector<std::string>* notes)
{
    std::vector<std::string> names;
    const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
    if (events == nullptr)
        return names;
    const bool senses = IsLevelTrigger(world, scene, entity);
    std::vector<const AuthoredEventDefinition*> found;
    for (const AuthoredEventId id : events->Live())
        if (const AuthoredEventDefinition* event = events->Get(id);
            event != nullptr && (senses || !IsSensorEvent(event->Name)))
            found.push_back(event);
    std::ranges::stable_sort(found, [](const AuthoredEventDefinition* a, const AuthoredEventDefinition* b) {
        return IsSensorEvent(a->Name) && !IsSensorEvent(b->Name);
    });
    for (const AuthoredEventDefinition* event : found)
    {
        names.push_back(event->Name);
        if (notes != nullptr)
            notes->push_back(event->Description);
    }
    return names;
}

std::string HeadingOf(const World& world, const std::string& key, std::string_view on)
{
    const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
    const AuthoredEventDefinition* event = events != nullptr ? events->Get(events->Find(on)) : nullptr;
    const std::string name = event != nullptr && !event->DisplayName.empty() ? event->DisplayName
                           : on.empty()                                     ? std::string("nothing")
                                                                            : std::string(on);
    return std::format("On {} \xC2\xB7 {}", name, key);
}

// What a binding's sequence may read: the entity, then its event's payload.
struct BindingVocabulary
{
    std::vector<std::string> Names;
    std::vector<SequencePort> Ports;

    std::string Instigator;

    // New commands start aimed at whoever caused the occurrence, as a bumper
    // launches whoever entered it.
    VerbSequenceVocabulary Of(const World& world, std::string_view on)
    {
        VerbSequenceStore::PortsFor(kResponsePorts, world, on, Names, Ports);
        const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
        const AuthoredEventDefinition* event = events != nullptr ? events->Get(events->Find(on)) : nullptr;
        Instigator = event != nullptr ? event->Instigator : std::string();
        return VerbSequenceVocabulary{ .Verbs = FindVerbRegistry(world),
                                       .Queries = FindAuthoredQueryRegistry(world),
                                       .Ports = Ports,
                                       .StartingPort = Instigator };
    }
};

// "b.<key>.<rest>"; keys are stable keys, so they hold no dot.
struct BindingKey
{
    std::string Binding;
    std::string_view Rest;
};

std::optional<BindingKey> ParseBindingKey(std::string_view key)
{
    if (!key.starts_with(kBindingPrefix))
        return std::nullopt;
    key.remove_prefix(kBindingPrefix.size());
    const std::size_t dot = key.find('.');
    BindingKey parsed{ std::string(key.substr(0, dot)),
                       dot == std::string_view::npos ? std::string_view{} : key.substr(dot + 1) };
    if (!IsValidStableKey(parsed.Binding))
        return std::nullopt;
    return parsed;
}

// A key no binding holds yet, from the event's last word: "enter", "impact".
std::string FreshKey(const JsonValue& bindings, std::string_view on)
{
    const std::size_t dot = on.rfind('.');
    std::string base(dot == std::string_view::npos ? on : on.substr(dot + 1));
    if (!IsValidStableKey(base))
        base = "response";
    std::string key = base;
    for (int suffix = 2; FindMember(bindings, key) != nullptr; ++suffix)
        key = std::format("{}_{}", base, suffix);
    return key;
}

class ResponsesEditorAdapter final : public IEditorComponentAdapter
{
public:
    [[nodiscard]] ComponentTypeId Type() const override { return ResolveComponentTypeId<Responses>(); }

    bool AuthorsInspectorRows() const override { return true; }
    bool InspectorRowsFollowFields() const override { return true; }

    std::string InspectorLabel(const EditorComponentInspectorContext& context) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        return IsLevelTrigger(world, context.Scene, context.Entity) ? "Trigger" : "Behaviors";
    }

    std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
    {
        World& world = context.Scene.GetRegistry().Components;
        const bool levelTrigger = IsLevelTrigger(world, context.Scene, context.Entity);
        const JsonValue bindings = BindingsOf(world, context.Entity, levelTrigger);
        ResponseDefinitions& definitions = ResponseDefinitionsOf(world);
        definitions.Sequences.Revalidate(world);
        definitions.Sets.Revalidate(world);

        std::vector<std::string> problems;
        if (levelTrigger)
            if (std::string unsensed = UnsensedReason(world, context.Scene, context.Entity); !unsensed.empty())
                problems.push_back(std::move(unsensed));
        if (const Responses* responses = world.TryGet<Responses>(context.Entity); responses != nullptr)
        {
            (void)definitions.Sets.Resolve(responses->Set, world);
            for (const std::string& diagnostic : definitions.Sets.Diagnostics(responses->Set))
                problems.push_back(diagnostic);
            for (const ResponseBindingDesc& binding : definitions.Sets.Bindings(responses->Set))
            {
                (void)definitions.Sequences.Resolve(binding.Sequence, world, nullptr, nullptr);
                for (const std::string& diagnostic : definitions.Sequences.Diagnostics(binding.Sequence))
                    problems.push_back(std::format("binding '{}': {}", binding.Key, diagnostic));
            }
        }

        std::vector<std::string> notes;
        const std::vector<std::string> events = EventChoices(world, context.Scene, context.Entity, &notes);
        PropertyNode list = GroupProperty("bindings", levelTrigger ? "Responses" : "Behaviors");
        list.Descriptor.Tooltip = "Each answers one occurrence at this entity, with its own conditions and commands.";
        PropertyVerb add{ .Id = "add", .Label = levelTrigger ? "Add Response" : "Add Behavior" };
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            add.Choices.push_back(events[i]);
            add.ChoiceNotes.push_back(notes[i]);
        }
        list.Descriptor.Verbs.push_back(std::move(add));

        std::vector<std::pair<std::string, const JsonValue*>> ordered;
        if (bindings.IsObject())
            for (const auto& [key, binding] : bindings.AsObject())
                ordered.emplace_back(key, &binding);
        std::ranges::sort(ordered, {}, &std::pair<std::string, const JsonValue*>::first);
        for (const auto& [key, binding] : ordered)
            list.Children.push_back(BindingCard(world, key, *binding, events));

        std::vector<PropertyNode> nodes;
        if (!problems.empty())
        {
            PropertyNode noted = GroupProperty("problems", std::format("Problems ({})", problems.size()));
            for (std::size_t i = 0; i < problems.size(); ++i)
                noted.Children.push_back(NoteProperty(std::format("problem{}", i), "", problems[i]));
            nodes.push_back(std::move(noted));
        }
        nodes.push_back(std::move(list));
        return nodes;
    }

    PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                         std::string_view value) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        const JsonValue before =
            BindingsOf(world, context.Entity, IsLevelTrigger(world, context.Scene, context.Entity));
        const std::optional<BindingKey> parsed = ParseBindingKey(key);
        JsonValue after = before;
        JsonValue* binding = parsed ? FindMember(after, parsed->Binding) : nullptr;
        if (binding == nullptr || !binding->IsObject())
            return PropertyEditResult::Refused;
        const std::string_view rest = parsed->Rest;
        if (rest.starts_with(kSequencePrefix))
        {
            const JsonValue* sequence = FindMember(*binding, "sequence");
            BindingVocabulary vocabulary;
            std::optional<JsonValue> edited =
                SetVerbSequenceValue(sequence != nullptr ? *sequence : EmptyVerbSequence(),
                                     vocabulary.Of(world, ReadMemberString(*binding, "on")),
                                     rest.substr(kSequencePrefix.size()), value);
            if (!edited)
                return PropertyEditResult::Refused;
            SetMember(*binding, "sequence", std::move(*edited));
        }
        else if (rest == "on")
        {
            const std::vector<std::string> events = EventChoices(world, context.Scene, context.Entity, nullptr);
            if (std::ranges::find(events, value) == events.end())
                return PropertyEditResult::Refused;
            SetMember(*binding, "on", JsonValue(std::string(value)));
        }
        else if (rest == "enabled" || rest == "once")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (!on)
                return PropertyEditResult::Refused;
            // Written only where it differs from the default, so the text says what was meant.
            if (*on == (rest == "enabled"))
                EraseMember(*binding, rest);
            else
                SetMember(*binding, rest, JsonValue(*on));
        }
        else if (rest == "cooldown")
        {
            const std::optional<double> seconds = ParseNumber(value, 0.0);
            if (!seconds)
                return PropertyEditResult::Refused;
            if (*seconds > 0.0)
                SetMember(*binding, "cooldown", JsonValue(*seconds));
            else
                EraseMember(*binding, "cooldown");
        }
        else if (rest == "cooldown_scope")
        {
            const std::optional<std::size_t> scope = ParseChoice(value, kScopes);
            if (!scope)
                return PropertyEditResult::Refused;
            if (*scope == 0)
                EraseMember(*binding, "cooldown_scope");
            else
                SetMember(*binding, "cooldown_scope", JsonValue(std::string("instigator")));
        }
        else
        {
            return PropertyEditResult::Refused;
        }
        return Commit(context, before, std::move(after));
    }

    PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                    std::string_view verb, std::string_view choice) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        const JsonValue before =
            BindingsOf(world, context.Entity, IsLevelTrigger(world, context.Scene, context.Entity));
        JsonValue after = before;
        if (key == "bindings" && verb == "add")
        {
            const std::vector<std::string> events = EventChoices(world, context.Scene, context.Entity, nullptr);
            if (std::ranges::find(events, choice) == events.end())
                return PropertyEditResult::Refused;
            SetMember(after, FreshKey(after, choice),
                      JsonValue(JsonValue::Object{ { "on", JsonValue(std::string(choice)) },
                                                   { "sequence", EmptyVerbSequence() } }));
            return Commit(context, before, std::move(after));
        }

        const std::optional<BindingKey> parsed = ParseBindingKey(key);
        JsonValue* binding = parsed ? FindMember(after, parsed->Binding) : nullptr;
        if (binding == nullptr)
            return PropertyEditResult::Refused;
        if (parsed->Rest.empty() && verb == "remove")
        {
            EraseMember(after, parsed->Binding);
            return Commit(context, before, std::move(after));
        }
        if (!parsed->Rest.starts_with(kSequencePrefix))
            return PropertyEditResult::Refused;
        const JsonValue* sequence = FindMember(*binding, "sequence");
        BindingVocabulary vocabulary;
        std::optional<JsonValue> edited =
            ActOnVerbSequence(sequence != nullptr ? *sequence : EmptyVerbSequence(),
                              vocabulary.Of(world, ReadMemberString(*binding, "on")),
                              parsed->Rest.substr(kSequencePrefix.size()), verb, choice);
        if (!edited)
            return PropertyEditResult::Refused;
        SetMember(*binding, "sequence", std::move(*edited));
        return Commit(context, before, std::move(after));
    }

    bool AuthorsViewportAffordances() const override { return true; }

    // Only a level trigger draws: an entity's behaviors are not a place in the level.
    void BuildViewport(const EditorComponentContext& context, ViewportAffordanceOutput& output) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        if (!IsLevelTrigger(world, context.Scene, context.Entity))
            return;
        const EditorVisual tint = *ComponentEditorVisual<Responses>::Value;
        const Vec4 colour = EditorTheme::DeclaredColour(tint.Edge);
        const std::optional<Aabb3d> bounds = context.Scene.EvaluatedWorldBounds(context.Entity);
        const Vec3d centre = bounds ? bounds->Center() : context.Transform.Position;
        const Vec3d half = bounds ? bounds->HalfExtent() : Vec3d(0.5f, 0.5f, 0.5f);
        output.Labels.push_back({ centre + Vec3d(0.0f, half.Y + 0.3f, 0.0f), colour, "TRIGGER" });

        // Every direction a command is given, from the volume's middle.
        const VerbRegistry* verbs = FindVerbRegistry(world);
        const JsonValue bindings = BindingsOf(world, context.Entity, true);
        if (verbs == nullptr || !bindings.IsObject())
            return;
        const float reach = std::max(1.0f, std::max({ half.X, half.Y, half.Z }));
        for (const auto& [key, binding] : bindings.AsObject())
        {
            const JsonValue* sequence = FindMember(binding, "sequence");
            const JsonValue* commands = sequence != nullptr ? FindMember(*sequence, "commands") : nullptr;
            if (commands == nullptr || !commands->IsArray())
                continue;
            for (const JsonValue& command : commands->AsArray())
                ArrowsOf(*verbs, command, context, centre, reach, colour, output);
        }
    }

private:
    static PropertyNode BindingCard(const World& world, const std::string& key, const JsonValue& binding,
                                    const std::vector<std::string>& events)
    {
        const std::string on = ReadMemberString(binding, "on");
        const std::string prefix = std::format("{}{}.", kBindingPrefix, key);
        PropertyNode card = GroupProperty(std::string(kBindingPrefix) + key, HeadingOf(world, key, on));
        card.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove", .Removes = true });

        std::vector<std::string_view> options(events.begin(), events.end());
        if (std::ranges::find(options, on) == options.end())
            options.push_back(on);
        const std::size_t current = static_cast<std::size_t>(std::ranges::find(options, on) - options.begin());
        card.Children.push_back(ChoiceProperty(prefix + "on", "Answers", options, current,
                                               "The occurrence at this entity that runs this binding."));
        const JsonValue* enabled = FindMember(binding, "enabled");
        card.Children.push_back(ToggleProperty(prefix + "enabled", "Starts enabled",
                                               enabled == nullptr || !enabled->IsBool() || enabled->AsBool(),
                                               "A disabled binding ignores occurrences until responses.enable."));
        const JsonValue* once = FindMember(binding, "once");
        card.Children.push_back(ToggleProperty(prefix + "once", "Once",
                                               once != nullptr && once->IsBool() && once->AsBool(),
                                               "After it runs this binding never runs again; the entity stays."));
        const JsonValue* cooldown = FindMember(binding, "cooldown");
        card.Children.push_back(NumberProperty(prefix + "cooldown", "Cooldown",
                                               cooldown != nullptr && cooldown->IsNumber() ? cooldown->AsNumber()
                                                                                           : 0.0,
                                               0.0, -1.0, 0.1, "Seconds after it runs before it can run again."));
        card.Children.push_back(ChoiceProperty(prefix + "cooldown_scope", "Cooldown applies to", kScopes,
                                               ReadMemberString(binding, "cooldown_scope") == "instigator" ? 1 : 0));

        const JsonValue* sequence = FindMember(binding, "sequence");
        BindingVocabulary vocabulary;
        std::vector<PropertyNode> rows = VerbSequenceRows(sequence != nullptr ? *sequence : EmptyVerbSequence(),
                                                          vocabulary.Of(world, on), {});
        PrefixPropertyKeys(rows, prefix + std::string(kSequencePrefix));
        card.Children.insert(card.Children.end(), std::make_move_iterator(rows.begin()),
                             std::make_move_iterator(rows.end()));
        return card;
    }

    static void ArrowsOf(const VerbRegistry& verbs, const JsonValue& command, const EditorComponentContext& context,
                         Vec3d centre, float reach, Vec4 colour, ViewportAffordanceOutput& output)
    {
        const VerbDefinition* verb = verbs.Get(verbs.Find(ReadMemberString(command, "verb")));
        const JsonValue* arguments = FindMember(command, "arguments");
        if (verb == nullptr || arguments == nullptr)
            return;
        for (const DataFieldSchema& field : verb->Arguments.Children)
        {
            const DataFieldSchema& shown = UnwrappedField(field);
            const JsonValue* argument = FindMember(*arguments, field.Key);
            const JsonValue* constant = argument != nullptr ? FindMember(*argument, "const") : nullptr;
            if (shown.Kind != DataFieldKind::Vector || shown.VectorLength != 3 || constant == nullptr
                || !constant->IsArray() || constant->AsArray().size() != 3)
                continue;
            const auto& parts = constant->AsArray();
            Vec3d direction(static_cast<float>(parts[0].AsNumber()), static_cast<float>(parts[1].AsNumber()),
                            static_cast<float>(parts[2].AsNumber()));
            if (direction.SqrMagnitude() <= 1.0e-8f)
                continue;
            if (ReadMemberString(*argument, "frame") == "self")
                direction = context.Transform.Rotation.RotateVector(direction);
            direction = direction.Normalized();
            const Vec3d across = std::abs(direction.Y) < 0.9f ? Vec3d(0.0f, 1.0f, 0.0f) : Vec3d(1.0f, 0.0f, 0.0f);
            AppendArrow(output, centre, centre + direction * reach, direction.Cross(across), colour);
        }
    }

    static PropertyEditResult Commit(EditorComponentInspectorContext& context, const JsonValue& before,
                                     JsonValue after)
    {
        if (JsonStringify(after) == JsonStringify(before))
            return PropertyEditResult::Unchanged;
        World* world = &context.Scene.GetRegistry().Components;
        const EntityId entity = context.Entity;
        context.Commands.Execute(std::make_unique<ValueCommand<JsonValue>>(
            before, std::move(after),
            [world, entity](const JsonValue& bindings) { (void)SetResponseBindings(*world, entity, bindings); },
            context.Document));
        return PropertyEditResult::Applied;
    }
};
} // namespace

std::unique_ptr<IEditorComponentAdapter> MakeResponsesEditorAdapter()
{
    return std::make_unique<ResponsesEditorAdapter>();
}
