#include <zone/ZoneScene.h>

#include <render/DefaultRenderScene.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

ZoneScene::ZoneScene(ZoneRuntime& zones, ZoneId zone, ZoneParticipation participation)
    : Registry_(zones.CreateZone(zone))
    , Transforms_(Registry_.Components.Register<TransformStore<Transform3f>>(PropagationOrder_))
    , Renderers_(Registry_.Components.Register<MeshRendererStore>())
    , Cameras_(Registry_.Components.Register<CameraStore>())
    , TransformPropagation_(Transforms_, Hierarchy_, PropagationOrder_)
{
    zones.SetParticipation(zone, participation);
}

Registry& ZoneScene::GetRegistry()
{
    return Registry_;
}

const Registry& ZoneScene::GetRegistry() const
{
    return Registry_;
}

TransformHierarchyService& ZoneScene::Hierarchy()
{
    return Hierarchy_;
}

TransformPropagationOrderService& ZoneScene::PropagationOrder()
{
    return PropagationOrder_;
}

TransformStore<Transform3f>& ZoneScene::Transforms()
{
    return Transforms_;
}

TransformPresentationStore<Transform3f>& ZoneScene::PresentationTransforms()
{
    return PresentationTransforms_;
}

MeshRendererStore& ZoneScene::MeshRenderers()
{
    return Renderers_;
}

CameraStore& ZoneScene::Cameras()
{
    return Cameras_;
}

ActiveCameraService& ZoneScene::ActiveCamera()
{
    return ActiveCamera_;
}

EntityId ZoneScene::CreateEntity(const Transform3f& local)
{
    EntityId entity = Registry_.Entities.Create();
    Hierarchy_.Register(entity);
    Transforms_.Add(entity, local);
    return entity;
}

bool ZoneScene::AddMeshRenderer(EntityId entity, MeshHandle mesh, MaterialHandle material)
{
    return Renderers_.Add(entity, MeshRendererComponent{
        .Mesh = mesh,
        .Material = material,
    });
}

bool ZoneScene::AddCamera(EntityId entity, const CameraComponent& camera, bool makeActive)
{
    const bool added = Cameras_.Add(entity, camera);
    if (added && makeActive)
        ActiveCamera_.SetActive(entity);
    return added;
}

void ZoneScene::SetActiveCamera(EntityId entity)
{
    ActiveCamera_.SetActive(entity);
}

void ZoneScene::BeginSimulationTick()
{
    PresentationTransforms_.BeginSimulationTick(Transforms_);
}

void ZoneScene::PropagateTransforms()
{
    TransformPropagation_.Propagate();
}

void ZoneScene::EndSimulationTick()
{
    PresentationTransforms_.EndSimulationTick(Transforms_);
}

void ZoneScene::ResetPresentationTransforms()
{
    PresentationTransforms_.Reset(Transforms_, Hierarchy_, PropagationOrder_);
}

DefaultRenderScene ZoneScene::BuildDefaultRenderScene(MeshService& meshes,
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
