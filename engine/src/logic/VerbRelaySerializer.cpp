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
    constexpr std::string_view kBindingHashKey = "binding_hash";

    VerbRelayAuthoring& AuthoringOf(World& world)
    {
        return world.HasResource<VerbRelayAuthoring>() ? world.GetResource<VerbRelayAuthoring>()
                                                       : world.AddResource<VerbRelayAuthoring>();
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

        const VerbRelayAuthoring* authoring =
            registry.Components.TryGetResource<VerbRelayAuthoring>();
        const VerbRelayAuthoring::Record* authored =
            authoring != nullptr ? authoring->Find(entity) : nullptr;

        // The path the handle names when it names one; what the scene said
        // otherwise. A load that could not resolve the asset must not save the
        // reference away.
        std::string bindingsPath;
        if (context.Assets != nullptr && relay->Bindings.IsValid())
            bindingsPath = context.Assets->GetPathForLease(AssetType::Data,
                                                          relay->Bindings.ToToken());
        if (bindingsPath.empty() && authored != nullptr)
            bindingsPath = authored->BindingsPath;

        archive.BeginObject(std::string_view{});
        if (!bindingsPath.empty()
            && !WriteSceneAssetRef(archive, kBindingsKey, bindingsPath, context))
            return false;
        if (relay->Binding.IsValid())
        {
            const std::string* text =
                authored != nullptr && !authored->KeyText.empty() ? &authored->KeyText
                : authoring != nullptr ? authoring->FindKeyText(relay->Binding)
                                       : nullptr;
            if (text != nullptr)
                archive.Field(kBindingKey, std::string_view(*text));
            else
                archive.Field(kBindingHashKey,
                              std::string_view(VerbBindingKeyToString(relay->Binding)));
        }
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
        std::string hashText;
        if (archive.HasField(kBindingHashKey))
            archive.Field(kBindingHashKey, hashText);
        archive.End();
        if (!read || !archive.Ok())
            return false;

        VerbRelay relay{};

        // The key's text is hashed; a hash field is parsed. Two fields, so a
        // spelling is never mistaken for a number.
        if (!keyText.empty())
            relay.Binding = MakeVerbBindingKey(keyText);
        else if (!hashText.empty() && !VerbBindingKeyFromString(hashText, relay.Binding))
        {
            context.Logging->GetLogger<SceneSerializationContext>().Error(
                "VerbRelay: '{}' is not a binding hash", hashText);
            archive.MarkInvalidField(kBindingHashKey);
            return false;
        }

        // What the scene said, kept whether or not the asset resolves below.
        AuthoringOf(world).Remember(entity, bindingsPath, keyText, relay.Binding);

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
