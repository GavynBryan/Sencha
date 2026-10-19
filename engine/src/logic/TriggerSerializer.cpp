#include <core/serialization/FourCC.h>
#include <cstring>
#include <logic/TriggerPrograms.h>
#include <logic/TriggerSerializer.h>

namespace
{
class TriggerSerializer final : public IComponentSerializer
{
  public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<Trigger>();
    }
    std::string_view JsonKey() const override
    {
        return "trigger";
    }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('T', 'R', 'G', 'R');
    }
    std::span<const RuntimeField> RuntimeFields() const override
    {
        return RuntimeFieldsOf<Trigger>();
    }
    std::vector<std::byte> DefaultBytes() const override
    {
        const Trigger value{};
        std::vector<std::byte> bytes(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
        return bytes;
    }
    void RegisterStorage(Registry &registry) const override
    {
        if (!registry.Components.IsRegistered<Trigger>())
            registry.Components.RegisterComponent<Trigger>();
    }
    bool HasComponent(EntityId entity, const Registry &registry) const override
    {
        return registry.Components.IsRegistered<Trigger>() && registry.Components.HasComponent<Trigger>(entity);
    }
    bool Save(IWriteArchive &archive, EntityId entity, const Registry &registry,
              SceneSerializationContext &) const override
    {
        const auto *trigger = registry.Components.TryGet<Trigger>(entity);
        if (trigger == nullptr)
            return true;
        const auto *source = TriggerProgramSource(registry.Components, entity);
        if (source == nullptr)
        {
            archive.MarkInvalidField("program");
            return false;
        }
        archive.BeginObject({});
        VisitSchema<TypeSchema<Trigger>>(*trigger, archive);
        archive.Value("program", *source);
        archive.End();
        return archive.Ok();
    }
    bool Load(IReadArchive &archive, EntityId entity, Registry &registry, SceneSerializationContext &context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }
    bool LoadIntoWorld(IReadArchive &archive, EntityId entity, World &world, SceneSerializationContext &) override
    {
        Trigger trigger;
        JsonValue source = EmptyTriggerProgram();
        archive.BeginObject({});
        VisitSchema<TypeSchema<Trigger>>(trigger, archive);
        if (archive.HasField("program"))
            archive.Value("program", source);
        archive.End();
        if (!archive.Ok())
            return false;
        if (world.HasComponent<Trigger>(entity))
        {
            if (!world.InitializeComponent<Trigger>(entity, trigger))
                return false;
        }
        else
            world.AddComponent<Trigger>(entity, trigger);
        return SetTriggerProgram(world, entity, std::move(source));
    }
    bool Remove(EntityId entity, Registry &registry) const override
    {
        if (HasComponent(entity, registry))
            registry.Components.RemoveComponent<Trigger>(entity);
        return true;
    }
};
} // namespace
std::unique_ptr<IComponentSerializer> MakeTriggerSerializer()
{
    return std::make_unique<TriggerSerializer>();
}
