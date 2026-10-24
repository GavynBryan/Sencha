#include <logic/TriggerSerializer.h>

#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <world/serialization/ComponentSerializer.h>

#include <cstring>
#include <string_view>
#include <tuple>

namespace
{
constexpr std::string_view kSequenceKey = "sequence";

class TriggerSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override { return ResolveComponentTypeId<Trigger>(); }
    std::string_view JsonKey() const override { return "trigger"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('T', 'R', 'I', 'G'); }

    std::span<const RuntimeField> RuntimeFields() const override { return RuntimeFieldsOf<Trigger>(); }
    std::optional<EditorVisual> GetEditorVisual() const override { return ComponentEditorVisual<Trigger>::Value; }

    std::vector<std::byte> DefaultBytes() const override
    {
        const Trigger value{};
        std::vector<std::byte> bytes(sizeof(Trigger));
        std::memcpy(bytes.data(), &value, sizeof(Trigger));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<Trigger>())
            registry.Components.RegisterComponent<Trigger>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<Trigger>() && registry.Components.HasComponent<Trigger>(entity);
    }

    bool Save(IWriteArchive& archive, EntityId entity, const Registry& registry,
              SceneSerializationContext& context) const override
    {
        const World& world = registry.Components;
        const Trigger* trigger = world.IsRegistered<Trigger>() ? world.TryGet<Trigger>(entity) : nullptr;
        if (trigger == nullptr)
            return true;
        archive.BeginObject(std::string_view{});
        bool ok = true;
        std::apply([&](const auto&... field) {
            ((ok = SceneComponentSerialization::SaveField(archive, *trigger, field, context) && ok), ...);
        }, TypeSchema<Trigger>::Fields());
        const JsonValue* sequence = TriggerSequenceSource(world, entity);
        archive.Subtree(kSequenceKey, sequence != nullptr ? *sequence : EmptyVerbSequence());
        archive.End();
        return ok && archive.Ok();
    }

    bool Load(IReadArchive& archive, EntityId entity, Registry& registry,
              SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        Trigger trigger{};
        archive.BeginObject(std::string_view{});
        bool ok = true;
        std::apply([&](const auto&... field) {
            (([&] {
                if (!archive.HasField(field.Name))
                {
                    if (field.DefaultValue)
                        trigger.*field.Ptr = *field.DefaultValue;
                    return;
                }
                ok = SceneComponentSerialization::LoadField(archive, trigger, field, context) && ok;
            }()), ...);
        }, TypeSchema<Trigger>::Fields());
        JsonValue sequence = EmptyVerbSequence();
        if (archive.HasField(kSequenceKey))
            archive.Subtree(kSequenceKey, sequence);
        archive.End();
        if (!ok || !archive.Ok())
            return false;

        // The load's reference holds the sequence across the add; the entity's
        // copy takes its own through OnAdd.
        VerbSequenceStore& store = TriggerSequencesOf(world);
        trigger.Sequence = store.Intern(sequence);
        const bool added = world.HasComponent<Trigger>(entity) ? world.InitializeComponent<Trigger>(entity, trigger)
                                                                : (world.AddComponent<Trigger>(entity, trigger), true);
        store.Release(trigger.Sequence);
        return added;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<Trigger>() && registry.Components.HasComponent<Trigger>(entity))
            registry.Components.RemoveComponent<Trigger>(entity);
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeTriggerSerializer()
{
    return std::make_unique<TriggerSerializer>();
}
