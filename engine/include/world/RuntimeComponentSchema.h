#pragma once

#include <string>

class ComponentSerializerRegistry;
class WorldComponentSchema;

// Adds every engine-owned component type that may appear in a runtime World.
//
// The order intentionally mirrors the current default zone initialization path:
// scene storage (including derived transform columns), physics, AbilityKit,
// movement, then camera runtime data. Keeping this prefix stable lets current
// registry Worlds and the future unified World coexist during migration.
void RegisterEngineRuntimeComponents(WorldComponentSchema& schema);

// Every serializable scene component must also have runtime storage in the
// sealed world vocabulary. Returns false and names the first missing serializer
// instead of allowing zone import to fail later and after partial publication.
bool RuntimeComponentSchemaCoversSerializers(
    const WorldComponentSchema& schema,
    const ComponentSerializerRegistry& serializers,
    std::string* missingComponent = nullptr);
