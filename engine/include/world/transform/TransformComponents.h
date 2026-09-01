#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// LocalTransform
//
// Authoritative local-space transform for an entity. This is the transform that
// scene serialization writes and gameplay systems edit directly.
//=============================================================================
struct LocalTransform
{
    Transform3f Value;
};

//=============================================================================
// WorldTransform
//
// Derived world-space transform for an entity. Transform propagation owns this
// component; it is reconstructed from LocalTransform and Parent after load.
//=============================================================================
struct WorldTransform
{
    Transform3f Value;
};

//=============================================================================
// Parent
//
// Optional hierarchy component. An entity has a spatial parent iff this
// component is present.
//=============================================================================
struct Parent
{
    EntityId Entity;
};

// WorldTransform and Parent are pure-runtime (never serialized themselves), so
// they carry no TypeSchema. They still need module-stable identity for the World
// type→id map — declared explicitly here.
//
// LocalTransform's identity used to be read off its TypeSchema::Name. Stating it
// here instead lets the schema move without the identity following it; the name
// is repeated exactly, because it is what every cooked scene and every peer
// already calls this component.
SENCHA_DECLARE_COMPONENT_TYPE(LocalTransform, "Transform");
SENCHA_COMPONENT_DECLARES_SCHEMA(LocalTransform);
SENCHA_DECLARE_COMPONENT_TYPE(WorldTransform, "sencha.world_transform");
SENCHA_DECLARE_COMPONENT_TYPE(Parent,         "sencha.parent");
