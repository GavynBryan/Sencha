#pragma once

#include <string>

class World;
class CollisionShapeCache;

//=============================================================================
// LoadZoneCollision
//
// Loads a level's cooked collision sidecar (written by the level cook): for each
// entry, restores the pre-baked Jolt blob into `cache` and spawns a static
// collider entity at its origin. PhysicsScene then turns those into static
// bodies; they die with the zone's registry. Returns the number loaded.
//
// This is how authored brush geometry becomes collidable: zero authoring, world
// collision loaded with the map. The World must have LocalTransform and Collider
// registered. A missing sidecar is not an error (a level with no brushes).
//
// cookedRoot is the directory the sidecar's relative blob paths resolve against
// (e.g. "assets/.cooked").
//=============================================================================
int LoadZoneCollision(World& world,
                      CollisionShapeCache& cache,
                      const std::string& sidecarPath,
                      const std::string& cookedRoot);
