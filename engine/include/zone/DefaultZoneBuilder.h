#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <ecs/EntityId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

class AudioClipCache;
class AudioService;
class Registry;
class ZoneRuntime;

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation = {},
                              StaticMeshCache* meshes = nullptr,
                              MaterialCache* materials = nullptr,
                              AudioClipCache* audioClips = nullptr,
                              AudioService* audio = nullptr);

// Registration-only part of CreateDefault3DZone, usable on a detached
// registry (async zone builds): components, resources, and the asset-store
// pointers. Storing the cache pointers is plain data — safe off-thread; the
// caches themselves are only dereferenced by main-thread consumers later.
void InitializeDefault3DRegistry(Registry& registry,
                                 StaticMeshCache* meshes = nullptr,
                                 MaterialCache* materials = nullptr,
                                 AudioClipCache* audioClips = nullptr,
                                 AudioService* audio = nullptr);

EntityId CreateDefaultEntity(Registry& registry,
                             const Transform3f& local = Transform3f::Identity());

bool AddDefaultMeshRenderer(Registry& registry,
                            EntityId entity,
                            StaticMeshHandle mesh,
                            MaterialHandle material);

bool AddDefaultCamera(Registry& registry,
                      EntityId entity,
                      const CameraComponent& camera,
                      bool makeActive = true);
