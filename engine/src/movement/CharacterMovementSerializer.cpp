#include <movement/CharacterMovementSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <world/serialization/TypedComponentSerializer.h>

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

    bool Leased(const World& world, EntityId entity, LocomotionModeId mode)
    {
        const LocomotionModeLease* lease =
            world.IsRegistered<LocomotionModeLease>() ? world.TryGet<LocomotionModeLease>(entity) : nullptr;
        return lease != nullptr && lease->Owner.IsSet() && lease->Mode == mode;
    }

class CharacterMovementSerializer final : public TypedComponentSerializer<CharacterMovement>
{
public:
    std::string_view JsonKey() const override { return "CharacterMovement"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('C', 'H', 'M', 'V');
    }

    std::span<const RuntimeField> RuntimeFields() const override { return {}; }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const World& world,
              SceneSerializationContext&) const override
    {
        if (!world.IsRegistered<CharacterMovement>())
            return true;

        const CharacterMovement* movement =
            world.TryGet<CharacterMovement>(entity);
        if (movement == nullptr)
            return true;

        archive.BeginObject(std::string_view{});

        // A mode this process cannot name writes nothing rather than a number
        // or an empty string: absence reads back as the free mode, which is
        // what an unnamed mode already means.
        const LocomotionModeRegistry* modes = world.TryGetResource<LocomotionModeRegistry>();
        const LocomotionModeEntry* mode = modes != nullptr ? modes->Find(movement->Mode) : nullptr;
        if (mode != nullptr && !Leased(world, entity, movement->Mode))
            archive.Field(kModeKey, std::string_view(mode->Name));

        archive.End();
        return archive.Ok();
    }

    bool SaveDefault(IWriteArchive& archive) const override
    {
        archive.BeginObject(std::string_view{});
        archive.End();
        return archive.Ok();
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

        return Place(world, entity, movement);
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeCharacterMovementSerializer()
{
    return std::make_unique<CharacterMovementSerializer>();
}
