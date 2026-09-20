#include <logic/VerbRelaySerializer.h>

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetLease.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/serialization/Archive.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>
#include <world/serialization/SceneAssetRef.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kBindingsKey = "bindings";
    constexpr std::string_view kBindingKey = "binding";

    // What the scene says for the key: the text this World has for it, or the
    // hash's digits when nothing ever spelled it here.
    std::string KeyTextFor(const VerbRelay& relay, const World& world)
    {
        if (const VerbRelayKeyNames* names = world.TryGetResource<VerbRelayKeyNames>())
        {
            if (const std::string* text = names->Find(relay.Binding))
                return *text;
        }
        return VerbBindingKeyToString(relay.Binding);
    }

class VerbRelaySerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override { return ResolveComponentTypeId<VerbRelay>(); }
    std::string_view JsonKey() const override { return "verb_relay"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('V', 'R', 'L', 'Y'); }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        return RuntimeFieldsOf<VerbRelay>();
    }

    std::vector<std::byte> DefaultBytes() const override
    {
        VerbRelay value{};
        std::vector<std::byte> bytes(sizeof(VerbRelay));
        std::memcpy(bytes.data(), &value, sizeof(VerbRelay));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<VerbRelay>())
            registry.Components.RegisterComponent<VerbRelay>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<VerbRelay>()
            && registry.Components.HasComponent<VerbRelay>(entity);
    }

    bool Save(IWriteArchive& archive, EntityId entity, const Registry& registry,
              SceneSerializationContext& context) const override
    {
        if (!registry.Components.IsRegistered<VerbRelay>())
            return true;
        const VerbRelay* relay = registry.Components.TryGet<VerbRelay>(entity);
        if (relay == nullptr)
            return true;

        archive.BeginObject(std::string_view{});
        if (context.Assets != nullptr && relay->Bindings.IsValid())
        {
            const std::string_view path =
                context.Assets->GetPathForLease(AssetType::Data, relay->Bindings.ToToken());
            if (!WriteSceneAssetRef(archive, kBindingsKey, path, context))
                return false;
        }
        if (relay->Binding.IsValid())
            archive.Field(kBindingKey,
                          std::string_view(KeyTextFor(*relay, registry.Components)));
        archive.End();
        return archive.Ok();
    }

    bool Load(IReadArchive& archive, EntityId entity, Registry& registry,
              SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        archive.BeginObject(std::string_view{});
        std::string bindingsPath;
        const bool stated = archive.HasField(kBindingsKey);
        const bool read = !stated
            || ReadSceneAssetRef(archive, kBindingsKey, AssetType::Data, bindingsPath, context);
        std::string keyText;
        if (archive.HasField(kBindingKey))
            archive.Field(kBindingKey, keyText);
        archive.End();
        if (!read || !archive.Ok())
            return false;

        VerbRelay relay{};

        // Text or digits, whichever the file has. Digits are what a scene keeps
        // when nothing could spell the key; text is what an author wrote, and
        // its hash is the same value the binding asset computes for its record.
        if (!keyText.empty() && !VerbBindingKeyFromString(keyText, relay.Binding))
        {
            relay.Binding = MakeVerbBindingKey(keyText);
            VerbRelayKeyNames& names = world.HasResource<VerbRelayKeyNames>()
                ? world.GetResource<VerbRelayKeyNames>()
                : world.AddResource<VerbRelayKeyNames>();
            names.Remember(relay.Binding, keyText);
        }

        if (!bindingsPath.empty() && context.Assets == nullptr)
        {
            context.Logging->GetLogger<SceneSerializationContext>().Error(
                "VerbRelay: no asset system to resolve binding set '{}'", bindingsPath);
            archive.MarkInvalidField(kBindingsKey);
            return false;
        }
        // The lease holds the set across the add; the entity's copy takes its
        // own reference through OnAdd. A set that will not load leaves the
        // relay unresolved, which the drain reports rather than a refused scene.
        AssetLease bindings;
        if (!bindingsPath.empty() && context.Assets != nullptr
            && context.Assets->HasStore(AssetType::Data))
        {
            bindings = context.Assets->LoadLease(bindingsPath, AssetType::Data);
            if (bindings.IsValid())
                relay.Bindings = DataAssetHandle::FromToken(bindings.OpaqueToken());
            else
                context.Logging->GetLogger<SceneSerializationContext>().Warn(
                    "VerbRelay: binding set '{}' did not load; the relay resolves nothing",
                    bindingsPath);
        }

        if (world.HasComponent<VerbRelay>(entity))
            return world.InitializeComponent<VerbRelay>(entity, relay);
        world.AddComponent<VerbRelay>(entity, relay);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<VerbRelay>()
            && registry.Components.HasComponent<VerbRelay>(entity))
        {
            registry.Components.RemoveComponent<VerbRelay>(entity);
        }
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeVerbRelaySerializer()
{
    return std::make_unique<VerbRelaySerializer>();
}
