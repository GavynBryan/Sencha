#include <world/RuntimeComponentSchema.h>

#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraRig.h>
#include <components/CameraComponent.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

void RegisterEngineRuntimeComponents(WorldComponentSchema& schema)
{
    // Default scene-storage prefix. LocalTransform's current storage traits
    // register its derived WorldTransform and Parent columns immediately after
    // it, so the schema preserves that exact order.
    schema.Add<LocalTransform>();
    schema.Add<WorldTransform>();
    schema.Add<Parent>();
    schema.Add<CameraComponent>();
    schema.Add<StaticMeshComponent>();
    schema.Add<ZoneLightmapComponent>();
    schema.Add<IrradianceVolumeComponent>();
    schema.Add<PointLightComponent>();
    schema.Add<SpotLightComponent>();
    schema.Add<AudioSourceComponent>();
    schema.Add<AudioCaptionComponent>();

    // Physics component and runtime-link family.
    schema.Add<Collider>();
    schema.Add<RigidBody>();
    schema.Add<CharacterController>();
    schema.Add<PhysicsBodyLink>();
    schema.Add<CharacterMoverLink>();

    // AbilityKit component storage. Registry resources remain a separate
    // runtime-world initialization concern.
    schema.Add<GameplayTagContainer>();
    schema.Add<AttributeSet>();
    schema.Add<AbilitySet>();
    schema.Add<ActiveEffect>();

    // Movement component family.
    schema.Add<MovementIntent>();
    schema.Add<MovementState>();
    schema.Add<MovementProfile>();
    schema.Add<OnGround>();
    schema.Add<InAir>();
    schema.Add<LocomotionModeRequest>();

    // Camera runtime data beyond the serializable CameraComponent.
    schema.Add<CameraRig>();
}

bool RuntimeComponentSchemaCoversSerializers(
    const WorldComponentSchema& schema,
    const ComponentSerializerRegistry& serializers,
    std::string* missingComponent)
{
    for (const auto& serializer : serializers.Entries())
    {
        if (schema.Contains(serializer->TypeId()))
            continue;

        if (missingComponent != nullptr)
            *missingComponent = std::string(serializer->JsonKey());
        return false;
    }

    if (missingComponent != nullptr)
        missingComponent->clear();
    return true;
}
