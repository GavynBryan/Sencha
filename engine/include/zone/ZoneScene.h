#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshTypes.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformPropagationSystem.h>
#include <world/transform/TransformStore.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

class MaterialStore;
class MeshRendererStore;
class MeshService;
class Registry;
class ZoneRuntime;
struct DefaultRenderScene;

class ZoneScene
{
public:
    ZoneScene(ZoneRuntime& zones, ZoneId zone, ZoneParticipation participation = {});

    ZoneScene(const ZoneScene&) = delete;
    ZoneScene& operator=(const ZoneScene&) = delete;
    ZoneScene(ZoneScene&&) = delete;
    ZoneScene& operator=(ZoneScene&&) = delete;

    [[nodiscard]] Registry& GetRegistry();
    [[nodiscard]] const Registry& GetRegistry() const;

    [[nodiscard]] TransformHierarchyService& Hierarchy();
    [[nodiscard]] TransformPropagationOrderService& PropagationOrder();
    [[nodiscard]] TransformStore<Transform3f>& Transforms();
    [[nodiscard]] MeshRendererStore& MeshRenderers();
    [[nodiscard]] CameraStore& Cameras();
    [[nodiscard]] ActiveCameraService& ActiveCamera();

    EntityId CreateEntity(const Transform3f& local = Transform3f::Identity());
    bool AddMeshRenderer(EntityId entity, MeshHandle mesh, MaterialHandle material);
    bool AddCamera(EntityId entity, const CameraComponent& camera, bool makeActive = true);
    void SetActiveCamera(EntityId entity);

    void PropagateTransforms();

    [[nodiscard]] DefaultRenderScene BuildDefaultRenderScene(
        MeshService& meshes,
        MaterialStore& materials);

private:
    Registry& Registry_;
    TransformHierarchyService Hierarchy_;
    TransformPropagationOrderService PropagationOrder_;
    TransformStore<Transform3f>& Transforms_;
    MeshRendererStore& Renderers_;
    CameraStore& Cameras_;
    ActiveCameraService ActiveCamera_;
    TransformPropagationSystem<Transform3f> TransformPropagation_;
};
