#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/DefaultRenderScene.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPresentationStore.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformPropagationSystem.h>
#include <world/transform/TransformStore.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>
#include <zone/ZoneRuntime.h>

#include <memory>

class ZoneScene
{
public:
    ZoneScene(ZoneRuntime& zones, ZoneId zone, ZoneParticipation participation = {});

    ZoneScene(const ZoneScene&) = delete;
    ZoneScene& operator=(const ZoneScene&) = delete;
    ZoneScene(ZoneScene&&) = delete;
    ZoneScene& operator=(ZoneScene&&) = delete;

    [[nodiscard]] Registry& GetRegistry() { return Registry_; }
    [[nodiscard]] const Registry& GetRegistry() const { return Registry_; }

    [[nodiscard]] TransformHierarchyService& Hierarchy() { return Hierarchy_; }
    [[nodiscard]] TransformPropagationOrderService& PropagationOrder() { return PropagationOrder_; }
    [[nodiscard]] TransformStore<Transform3f>& Transforms() { return Transforms_; }
    [[nodiscard]] TransformPresentationStore<Transform3f>& PresentationTransforms()
    {
        return PresentationTransforms_;
    }
    [[nodiscard]] MeshRendererStore& MeshRenderers() { return Renderers_; }
    [[nodiscard]] CameraStore& Cameras() { return Cameras_; }
    [[nodiscard]] ActiveCameraService& ActiveCamera() { return ActiveCamera_; }

    EntityId CreateEntity(const Transform3f& local = Transform3f::Identity());
    bool AddMeshRenderer(EntityId entity, MeshHandle mesh, MaterialHandle material);
    bool AddCamera(EntityId entity, const CameraComponent& camera, bool makeActive = true);
    void SetActiveCamera(EntityId entity) { ActiveCamera_.SetActive(entity); }

    void BeginSimulationTick();
    void PropagateTransforms();
    void EndSimulationTick();
    void ResetPresentationTransforms();

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
    TransformPresentationStore<Transform3f> PresentationTransforms_;
    TransformPropagationSystem<Transform3f> TransformPropagation_;
};

inline ZoneScene::ZoneScene(ZoneRuntime& zones, ZoneId zone, ZoneParticipation participation)
    : Registry_(zones.CreateZone(zone))
    , Transforms_(Registry_.Components.Register<TransformStore<Transform3f>>(PropagationOrder_))
    , Renderers_(Registry_.Components.Register<MeshRendererStore>())
    , Cameras_(Registry_.Components.Register<CameraStore>())
    , TransformPropagation_(Transforms_, Hierarchy_, PropagationOrder_)
{
    zones.SetParticipation(zone, participation);
}

inline EntityId ZoneScene::CreateEntity(const Transform3f& local)
{
    EntityId entity = Registry_.Entities.Create();
    Hierarchy_.Register(entity);
    Transforms_.Add(entity, local);
    return entity;
}

inline bool ZoneScene::AddMeshRenderer(EntityId entity,
                                       MeshHandle mesh,
                                       MaterialHandle material)
{
    return Renderers_.Add(entity, MeshRendererComponent{
        .Mesh = mesh,
        .Material = material,
    });
}

inline bool ZoneScene::AddCamera(EntityId entity,
                                 const CameraComponent& camera,
                                 bool makeActive)
{
    const bool added = Cameras_.Add(entity, camera);
    if (added && makeActive)
        ActiveCamera_.SetActive(entity);
    return added;
}

inline void ZoneScene::BeginSimulationTick()
{
    PresentationTransforms_.BeginSimulationTick(Transforms_);
}

inline void ZoneScene::PropagateTransforms()
{
    TransformPropagation_.Propagate();
}

inline void ZoneScene::EndSimulationTick()
{
    PresentationTransforms_.EndSimulationTick(Transforms_);
}

inline void ZoneScene::ResetPresentationTransforms()
{
    PresentationTransforms_.Reset(Transforms_, Hierarchy_, PropagationOrder_);
}

inline DefaultRenderScene ZoneScene::BuildDefaultRenderScene(
    MeshService& meshes,
    MaterialStore& materials)
{
    return DefaultRenderScene{
        .Hierarchy = &Hierarchy_,
        .PropagationOrder = &PropagationOrder_,
        .Transforms = &Transforms_,
        .PresentationTransforms = &PresentationTransforms_,
        .Renderers = &Renderers_,
        .Cameras = &Cameras_,
        .ActiveCamera = &ActiveCamera_,
        .Meshes = &meshes,
        .Materials = &materials,
    };
}
