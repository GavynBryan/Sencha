#pragma once

#include <ecs/ComponentAnnotations.h>

#include <type_traits>

// Marks an entity whose Collider changes walkable space at runtime: a bridge
// that extends, a crate that blocks a corridor, a platform that appears. When
// such an entity is added, removed, or moved, navigation rebuilds the tiles it
// touches. Opt-in, so ordinary moving bodies never churn navigation; cooked
// static geometry is already in the navmesh and never carries it.
//
// Primitive colliders (box, sphere, capsule) contribute. A collider that
// references a cooked mesh shape does not yet -- there is no backend-neutral
// copy of its triangles at runtime -- and is reported rather than ignored.
//
// Runtime-only, like Collider itself: no schema and no scene chunk.
struct SENCHA_COMPONENT("sencha.navigation.geometry") NavigationGeometry
{
};

static_assert(std::is_empty_v<NavigationGeometry>, "NavigationGeometry is a tag");

#if !defined(SENCHA_CODEGEN)
#  include <navigation/NavigationGeometry.sencha.h>
#endif
