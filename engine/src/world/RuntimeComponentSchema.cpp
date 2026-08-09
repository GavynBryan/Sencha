#include <world/RuntimeComponentSchema.h>

#include <abilities/AbilityKit.h>
#include <audio/AudioComponentRegistration.h>
#include <camera/CameraComponentRegistration.h>
#include <controller/ControllerComponentRegistration.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputComponentRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/NetComponentRegistration.h>
#include <net/ReplicationLayout.h>
#include <physics/PhysicsRegistration.h>
#include <render/RenderComponentRegistration.h>
#include <world/ComponentRegistrar.h>
#include <world/WorldComponentRegistration.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <zone/ZoneComponentRegistration.h>

void RegisterEngineComponents(ComponentRegistrar& registrar)
{
    // Wiring only. Every component the engine owns is named by the feature that
    // owns it; this decides the order those features are asked, and nothing
    // else. A component added to a feature reaches storage, scenes, and the
    // wire from that one edit, because its schema is what says which of those
    // apply.
    //
    // Order is a contract in one direction only. It fixes the dense component
    // index inside a World and the one-byte component key on the wire, both of
    // which are session-transient values two machines only have to agree on
    // because they are the same build -- the handshake compares the compiled
    // table's hash and refuses a peer that is not. Nothing persisted depends on
    // it: a component's saved identity is the chunk id its schema declares, and
    // its runtime identity is a hash of its stable name.
    //
    // The transform trio leads because the transform columns are the ones every
    // other system indexes against, and reading a World's vocabulary is easier
    // when position comes first.
    RegisterWorldComponents(registrar);

    RegisterRenderComponents(registrar);
    RegisterAudioComponents(registrar);
    RegisterZoneComponents(registrar);
    RegisterCameraComponents(registrar);

    RegisterPhysicsComponents(registrar);

    // Movement pulls the ability kit in itself: it is built on the kit, so the
    // two cannot be registered apart.
    RegisterMovementComponents(registrar);

    RegisterControllerComponents(registrar);
    RegisterInputComponents(registrar);
    RegisterNetComponents(registrar);
}

void RegisterEngineRuntimeComponents(WorldComponentSchema& schema)
{
    ComponentRegistrar registrar(&schema, nullptr, nullptr);
    RegisterEngineComponents(registrar);
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
