#include <world/RuntimeComponentSchema.h>

#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <camera/CameraRig.h>
#include <controller/LookOrientation.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationLayout.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentManifest.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

void RegisterEngineRuntimeComponents(WorldComponentSchema& schema)
{
    // The transform trio leads the schema. WorldTransform and Parent are
    // derived columns rather than serialized scene components, so they are not
    // in the manifest, and the canonical component ids depend on this order.
    schema.Add<LocalTransform>();
    schema.Add<WorldTransform>();
    schema.Add<Parent>();

    // Every serialized scene component needs storage. Folding over the manifest
    // is what keeps that true: a component added to the manifest for the
    // serializer cannot reach startup without a column to deserialize into.
    // Add is idempotent, so LocalTransform above is not registered twice.
    ForEachSceneComponent([&]<typename T>(ComponentTag<T>) { schema.Add<T>(); });

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

    // Movement component family: the physical facts, the character's authored
    // binding, this tick's resolved coefficients, and the contribution
    // channels that compose into one motor request.
    schema.Add<MovementIntent>();
    schema.Add<KinematicState>();
    schema.Add<SupportState>();
    schema.Add<Immersion>();
    schema.Add<CharacterMovement>();
    schema.Add<ResolvedMovementTuning>();
    schema.Add<LocomotionOutput>();
    schema.Add<MotionAxisOverride>();
    schema.Add<MotionImpulse>();
    schema.Add<MotionRequest>();
    schema.Add<ModeTransitionRequest>();
    schema.Add<ClingSession>();
    schema.Add<FlightSession>();

    // Camera runtime data beyond the serializable CameraComponent.
    schema.Add<CameraRig>();

    // Where a controlled entity is aiming, and the tag marking the one the
    // local player's look action drives.
    schema.Add<LookOrientation>();
    schema.Add<LocalLookControl>();

    // Per-tick pose history for entities that opt into render interpolation.
    // Derived and runtime-only, like WorldTransform: never serialized.
    schema.Add<WorldTransformHistory>();

    // Session data: which entities travel and whose inputs drive them. Present
    // in every build because a World's component vocabulary is fixed before any
    // session can exist; they cost a column each and nothing else when no
    // session is ever created.
    schema.Add<NetReplicated>();
    schema.Add<NetOwner>();
}

void RegisterEngineReplicatedComponents(ReplicationLayout& layout)
{
    ForEachReplicatedComponent([&]<typename T>(ComponentTag<T>) { layout.Add<T>(); });
}

bool RuntimeComponentSchemaCoversReplication(
    const WorldComponentSchema& schema,
    const ReplicationLayout& layout,
    std::string* missingComponent)
{
    for (const ReplicatedComponent& component : layout.Components())
    {
        if (schema.Contains(component.Type))
            continue;

        if (missingComponent != nullptr)
            *missingComponent = std::string(component.Name);
        return false;
    }

    if (missingComponent != nullptr)
        missingComponent->clear();
    return true;
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
