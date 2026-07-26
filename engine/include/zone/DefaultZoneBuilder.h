#pragma once

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/MaterialSetCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <render/static_mesh/StaticMeshHandle.h>

class AudioClipCache;
class AudioService;
class CaptionRuntime;
struct Registry;
class TextureCache;

// Editor/document convenience helpers for Registry-backed scenes. Runtime zone
// construction does not belong here; streamed runtime content is built as a
// ZoneLoadPackage and imported into RuntimeWorld storage partitions.
void InitializeDefault3DRegistry(
    Registry& registry,
    StaticMeshCache* meshes = nullptr,
    MaterialSetCache* materialSets = nullptr,
    AudioClipCache* audioClips = nullptr,
    AudioService* audio = nullptr,
    CaptionRuntime* captions = nullptr,
    TextureCache* textures = nullptr);

EntityId CreateDefaultEntity(
    Registry& registry,
    const Transform3f& local = Transform3f::Identity());

bool AddDefaultMeshRenderer(
    Registry& registry,
    EntityId entity,
    StaticMeshHandle mesh,
    MaterialSetHandle materials);

bool AddDefaultCamera(
    Registry& registry,
    EntityId entity,
    const CameraComponent& camera,
    bool makeActive = true);
