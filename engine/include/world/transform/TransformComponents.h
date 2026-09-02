#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

class World;
struct Registry;
template <typename T> struct ComponentStorageTraits;

//=============================================================================
// LocalTransform
//
// Authoritative local-space transform for an entity. This is the transform that
// scene serialization writes and gameplay systems edit directly.
//
// Replicated because where a thing is is the one fact every peer needs about
// every entity it can see. Predicted because a player moves the moment the key
// goes down rather than a round trip later, so their own machine keeps
// simulating where they are and treats what arrives as the authority's view to
// resume from.
//
// Not removable: it is paired with the derived WorldTransform, so the editor
// must not let it be stripped (that would orphan the pairing). A transform-less
// entity is made by never adding one.
//=============================================================================
struct SENCHA_COMPONENT("Transform")
       SENCHA_SCHEMA("Transform")
       SENCHA_SCENE_CHUNK("XFRM")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
       SENCHA_NON_REMOVABLE
LocalTransform
{
    SENCHA_FIELD("local")
    Transform3f Value;
};

//=============================================================================
// WorldTransform
//
// Derived world-space transform for an entity. Transform propagation owns this
// component; it is reconstructed from LocalTransform and Parent after load.
//=============================================================================
struct SENCHA_COMPONENT("sencha.world_transform") WorldTransform
{
    Transform3f Value;
};

//=============================================================================
// Parent
//
// Optional hierarchy component. An entity has a spatial parent iff this
// component is present.
//=============================================================================
struct SENCHA_COMPONENT("sencha.parent") Parent
{
    EntityId Entity;
};

// LocalTransform's registration is structural: WorldTransform and Parent are
// not serialized themselves (hierarchy travels separately and WorldTransform is
// derived), so they register alongside it, and every loaded LocalTransform
// seeds a matching WorldTransform for propagation.
template <>
struct ComponentStorageTraits<LocalTransform>
{
    static void Register(World& world);
    static void Register(Registry& registry);
    static bool Add(World& world, EntityId entity, LocalTransform component);
    static bool Add(Registry& registry, EntityId entity, LocalTransform component);
};

#if !defined(SENCHA_CODEGEN)
#  include <world/transform/TransformComponents.sencha.h>
#endif
