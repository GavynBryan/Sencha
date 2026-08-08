#pragma once

#include <string>

class ComponentRegistrar;
class ComponentSerializerRegistry;
class ReplicationLayout;
class WorldComponentSchema;

// Composes the engine's component vocabulary by asking each owning feature, in
// a fixed order, to name what it owns. The registrar decides what follows from
// each component -- storage, a scene serializer, a place on the wire -- by
// reading the component's own schema, so this is wiring and nothing else.
//
// A host calls this once with the registries it owns. What a game module adds
// goes through the same registrar afterwards, so its components take runtime
// indices and wire keys after the engine's.
void RegisterEngineComponents(ComponentRegistrar& registrar);

// Storage only, for a caller that wants the vocabulary without a serializer
// registry or a replicated table: the streaming and zone fixtures that seal a
// schema and apply it to a World.
void RegisterEngineRuntimeComponents(WorldComponentSchema& schema);

// Every serializable scene component must also have runtime storage in the
// sealed world vocabulary. Returns false and names the first missing serializer
// instead of allowing zone import to fail later and after partial publication.
bool RuntimeComponentSchemaCoversSerializers(
    const WorldComponentSchema& schema,
    const ComponentSerializerRegistry& serializers,
    std::string* missingComponent = nullptr);

// Every replicated component must have runtime storage, or a snapshot would
// name a component the receiving world has no column for. Returns false and
// names the first one missing.
bool RuntimeComponentSchemaCoversReplication(
    const WorldComponentSchema& schema,
    const ReplicationLayout& layout,
    std::string* missingComponent = nullptr);
