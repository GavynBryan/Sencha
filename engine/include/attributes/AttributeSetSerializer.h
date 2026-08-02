#pragma once

//=============================================================================
// AttributeSet scene-serializer registration
//
// Registers an IComponentSerializer for AttributeSet with the scene serializer.
// Attributes persist by name (see AttributeSerialization.h), resolved through
// the AttributeRegistry stored as a world resource on the registry being
// (de)serialized. Call once at startup, on the host's serializer registry.
//=============================================================================

class ComponentSerializerRegistry;

void RegisterAttributeSerializer(ComponentSerializerRegistry& serializers);
