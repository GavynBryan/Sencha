#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MeshRendererStore.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/entity/EntityId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

class Registry;
class ZoneRuntime;

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation = {},
                              StaticMeshCache* meshes = nullptr,
                              MaterialCache* materials = nullptr);

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
