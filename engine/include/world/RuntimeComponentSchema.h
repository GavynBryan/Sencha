#pragma once

#include <string>

class ComponentSerializerRegistry;
class ReplicationLayout;
class WorldComponentSchema;

// Adds every engine-owned component type that may appear in a runtime World.
//
// The order intentionally mirrors the current default zone initialization path:
// scene storage (including derived transform columns), physics, AbilityKit,
// movement, then camera runtime data. Keeping this prefix stable lets current
// registry Worlds and the future unified World coexist during migration.
void RegisterEngineRuntimeComponents(WorldComponentSchema& schema);

// Compiles the engine's replicated component table. Folds the replication
// manifest the same way the runtime schema folds the scene manifest, so the
// table cannot drift from the components it describes. A game module appends
// its own after this and before the layout is sealed.
void RegisterEngineReplicatedComponents(ReplicationLayout& layout);

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
