#include <movement/CharacterMovementSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponents.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kModeKey = "mode";

class CharacterMovementSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<CharacterMovement>();
    }

    std::string_view JsonKey() const override { return "CharacterMovement"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('C', 'H', 'M', 'V');
    }

    std::span<const RuntimeField> RuntimeFields() const override { return {}; }

    std::vector<std::byte> DefaultBytes() const override
    {
        CharacterMovement value{};
        std::vector<std::byte> bytes(sizeof(CharacterMovement));
        std::memcpy(bytes.data(), &value, sizeof(CharacterMovement));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<CharacterMovement>())
            registry.Components.RegisterComponent<CharacterMovement>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<CharacterMovement>()
            && registry.Components.HasComponent<CharacterMovement>(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext&) const override
    {
        if (!registry.Components.IsRegistered<CharacterMovement>())
            return true;

        const CharacterMovement* movement =
            registry.Components.TryGet<CharacterMovement>(entity);
        if (movement == nullptr)
            return true;

        archive.BeginObject(std::string_view{});

        // A mode this process cannot name writes nothing rather than a number
        // or an empty string: absence reads back as the free mode, which is
        // what an unnamed mode already means.
        if (const LocomotionModeRegistry* modes =
                registry.Components.TryGetResource<LocomotionModeRegistry>())
        {
            if (const LocomotionModeEntry* mode = modes->Find(movement->Mode))
                archive.Field(kModeKey, std::string_view(mode->Name));
        }

        archive.End();
        return archive.Ok();
    }

    bool Load(IReadArchive& archive,
              EntityId entity,
              Registry& registry,
              SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive,
                       EntityId entity,
                       World& world,
                       SceneSerializationContext& context) override
    {
        archive.BeginObject(std::string_view{});
        std::string modeName;
        if (archive.HasField(kModeKey))
            archive.Field(kModeKey, modeName);
        archive.End();
        if (!archive.Ok())
            return false;

        CharacterMovement movement{};
        if (const LocomotionModeRegistry* modes =
                world.TryGetResource<LocomotionModeRegistry>())
        {
            const LocomotionModeEntry* named =
                modeName.empty() ? nullptr : modes->Find(modeName);
            if (named != nullptr)
            {
                movement.Mode = named->Id;
            }
            else
            {
                if (!modeName.empty())
                {
                    context.Logging->GetLogger<SceneSerializationContext>().Warn(
                        "CharacterMovement: no locomotion mode named '{}' is "
                        "registered; the character starts in the free mode",
                        modeName);
                }
                movement.Mode = modes->FreeMode();
            }
        }

        if (world.HasComponent<CharacterMovement>(entity))
            return world.InitializeComponent<CharacterMovement>(entity, movement);

        world.AddComponent<CharacterMovement>(entity, movement);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<CharacterMovement>()
            && registry.Components.HasComponent<CharacterMovement>(entity))
        {
            registry.Components.RemoveComponent<CharacterMovement>(entity);
        }
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeCharacterMovementSerializer()
{
    return std::make_unique<CharacterMovementSerializer>();
}
