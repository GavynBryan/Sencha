#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshTypes.h>
#include <world/entity/EntityId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

class Registry;
class ZoneRuntime;

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation = {});

EntityId CreateDefaultEntity(Registry& registry,
                             const Transform3f& local = Transform3f::Identity());

bool AddDefaultMeshRenderer(Registry& registry,
                            EntityId entity,
                            MeshHandle mesh,
                            MaterialHandle material);

bool AddDefaultCamera(Registry& registry,
                      EntityId entity,
                      const CameraComponent& camera,
                      bool makeActive = true);
