#include "authoring/TriggerEditorAdapter.h"

#include "authoring/AffordanceStrokes.h"
#include "commands/CommandStack.h"
#include "data/JsonObjectEdit.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "vocabulary/AuthoredConstantText.h"
#include "vocabulary/VerbSequenceRows.h"

#include <authored/WorldVocabulary.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <physics/components/Collider.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
VerbSequenceVocabulary VocabularyOf(const World& world)
{
    return VerbSequenceVocabulary{ .Verbs = FindVerbRegistry(world),
                                   .Queries = FindAuthoredQueryRegistry(world),
                                   .Ports = kTriggerPorts };
}

JsonValue SequenceOf(const World& world, EntityId entity)
{
    const JsonValue* sequence = TriggerSequenceSource(world, entity);
    return sequence != nullptr ? *sequence : EmptyVerbSequence();
}

template <typename T>
bool Carries(const World& world, EntityId entity)
{
    return world.IsRegistered<T>() && world.HasComponent<T>(entity);
}

// Why nothing would activate this trigger, if nothing would.
std::string UnsensedReason(const World& world, const EditorScene& scene, EntityId entity)
{
    if (scene.TryGetBrush(entity) != nullptr)
    {
        if (!Carries<BrushVolume>(world, entity))
            return "This brush is solid geometry: make it a trigger volume so it senses rather than blocks.";
        if (!Carries<VolumeSensor>(world, entity))
            return "This volume does not cook to a sensor: add a volume sensor, or nothing activates it.";
        return {};
    }
    const Collider* collider = world.IsRegistered<Collider>() ? world.TryGet<Collider>(entity) : nullptr;
    if (collider == nullptr || !collider->IsTrigger)
        return "Nothing senses for this trigger: give it a sensor collider.";
    return {};
}


class TriggerEditorAdapter final : public IEditorComponentAdapter
{
public:
    [[nodiscard]] ComponentTypeId Type() const override { return ResolveComponentTypeId<Trigger>(); }

    bool AuthorsInspectorRows() const override { return true; }
    bool InspectorRowsFollowFields() const override { return true; }

    std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
    {
        World& world = context.Scene.GetRegistry().Components;
        std::vector<std::string> problems;
        if (std::string unsensed = UnsensedReason(world, context.Scene, context.Entity); !unsensed.empty())
            problems.push_back(std::move(unsensed));
        if (const Trigger* trigger = world.TryGet<Trigger>(context.Entity); trigger != nullptr)
        {
            VerbSequenceStore& store = TriggerSequencesOf(world);
            (void)store.Resolve(trigger->Sequence, world, nullptr, nullptr);
            for (const std::string& diagnostic : store.Diagnostics(trigger->Sequence))
                problems.push_back(diagnostic);
        }
        return VerbSequenceRows(SequenceOf(world, context.Entity), VocabularyOf(world), problems);
    }

    PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                         std::string_view value) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        const JsonValue before = SequenceOf(world, context.Entity);
        std::optional<JsonValue> after = SetVerbSequenceValue(before, VocabularyOf(world), key, value);
        return Commit(context, before, std::move(after));
    }

    PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                    std::string_view verb, std::string_view choice) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        const JsonValue before = SequenceOf(world, context.Entity);
        std::optional<JsonValue> after = ActOnVerbSequence(before, VocabularyOf(world), key, verb, choice);
        return Commit(context, before, std::move(after));
    }

    bool AuthorsViewportAffordances() const override { return true; }

    void BuildViewport(const EditorComponentContext& context, ViewportAffordanceOutput& output) const override
    {
        const World& world = context.Scene.GetRegistry().Components;
        const EditorVisual tint = *ComponentEditorVisual<Trigger>::Value;
        const Vec4 colour = EditorTheme::DeclaredColour(tint.Edge);
        const std::optional<Aabb3d> bounds = context.Scene.EvaluatedWorldBounds(context.Entity);
        const Vec3d centre = bounds ? bounds->Center() : context.Transform.Position;
        const Vec3d half = bounds ? bounds->HalfExtent() : Vec3d(0.5f, 0.5f, 0.5f);
        output.Labels.push_back({ centre + Vec3d(0.0f, half.Y + 0.3f, 0.0f), colour, "TRIGGER" });

        // Every direction a command is given, from the volume's middle.
        const VerbSequenceVocabulary vocabulary = VocabularyOf(world);
        const JsonValue sequence = SequenceOf(world, context.Entity);
        const JsonValue* commands = FindMember(sequence, "commands");
        if (commands == nullptr || !commands->IsArray() || vocabulary.Verbs == nullptr)
            return;
        const float reach = std::max(1.0f, std::max({ half.X, half.Y, half.Z }));
        for (const JsonValue& command : commands->AsArray())
        {
            const VerbDefinition* verb =
                vocabulary.Verbs->Get(vocabulary.Verbs->Find(ReadMemberString(command, "verb")));
            const JsonValue* arguments = FindMember(command, "arguments");
            if (verb == nullptr || arguments == nullptr)
                continue;
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
                if (ReadMemberString(*argument, "frame") == "trigger")
                    direction = context.Transform.Rotation.RotateVector(direction);
                direction = direction.Normalized();
                const Vec3d across = std::abs(direction.Y) < 0.9f ? Vec3d(0.0f, 1.0f, 0.0f) : Vec3d(1.0f, 0.0f, 0.0f);
                AppendArrow(output, centre, centre + direction * reach, direction.Cross(across), colour);
            }
        }
    }

private:
    static PropertyEditResult Commit(EditorComponentInspectorContext& context, const JsonValue& before,
                                     std::optional<JsonValue> after)
    {
        if (!after)
            return PropertyEditResult::Refused;
        if (JsonStringify(*after) == JsonStringify(before))
            return PropertyEditResult::Unchanged;
        World* world = &context.Scene.GetRegistry().Components;
        const EntityId entity = context.Entity;
        context.Commands.Execute(std::make_unique<ValueCommand<JsonValue>>(
            before, std::move(*after),
            [world, entity](const JsonValue& sequence) { (void)SetTriggerSequence(*world, entity, sequence); },
            context.Document));
        return PropertyEditResult::Applied;
    }
};
} // namespace

std::unique_ptr<IEditorComponentAdapter> MakeTriggerEditorAdapter()
{
    return std::make_unique<TriggerEditorAdapter>();
}
