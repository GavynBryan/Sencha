#include <movement/CharacterMovementSerializer.h>

#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <world/serialization/ComponentSerializer.h>

namespace
{
    bool Leased(const World& world, EntityId entity, LocomotionModeId mode)
    {
        const LocomotionModeLease* lease =
            world.IsRegistered<LocomotionModeLease>() ? world.TryGet<LocomotionModeLease>(entity) : nullptr;
        return lease != nullptr && lease->Owner.IsSet() && lease->Mode == mode;
    }

    class CharacterMovementSerializer final : public ComponentSerializer<CharacterMovement>
    {
    public:
        bool SavedFormFromRowAlone() const override { return false; }

        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            const CharacterMovement* movement =
                world.IsRegistered<CharacterMovement>() ? world.TryGet<CharacterMovement>(entity) : nullptr;
            if (movement == nullptr || !Leased(world, entity, movement->Mode))
                return ComponentSerializer<CharacterMovement>::Save(archive, entity, world, context);
            CharacterMovement unleased = *movement;
            unleased.Mode = {};
            return SceneComponentSerialization::SaveFields(archive, unleased, context, &world);
        }
    };
}

std::unique_ptr<IComponentSerializer> MakeCharacterMovementSerializer()
{
    return std::make_unique<CharacterMovementSerializer>();
}
