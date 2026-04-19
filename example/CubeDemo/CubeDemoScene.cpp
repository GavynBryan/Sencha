#include "CubeDemoScene.h"

#include <math/Quat.h>
#include <render/Camera.h>
#include <render/MeshRendererComponent.h>
#include <world/transform/TransformHierarchyService.h>
#include <zone/DefaultZoneBuilder.h>

namespace
{
    EntityId AddCube(Registry& registry,
                     MeshHandle mesh,
                     MaterialHandle material,
                     const Vec3d& position,
                     const Vec3d& scale)
    {
        EntityId entity = CreateDefaultEntity(
            registry,
            Transform3f(position, Quatf::Identity(), scale));
        AddDefaultMeshRenderer(registry, entity, mesh, material);
        return entity;
    }
}

TransformStore<Transform3f>& DemoTransforms(Registry& registry)
{
    return registry.Components.Get<TransformStore<Transform3f>>();
}

DemoScene CreateDemoScene(Registry& registry,
                          MeshService& meshes,
                          MaterialStore& materials,
                          FreeCamera& freeCamera)
{
    DemoScene scene;
    scene.CubeMesh = meshes.Create(MeshPrimitives::BuildCube(1.0f));
    scene.Red = materials.Create(Material{
        .Pass = ShaderPassId::ForwardOpaque,
        .BaseColor = Vec4(1.0f, 0.15f, 0.1f, 1.0f),
    });
    scene.Green = materials.Create(Material{
        .Pass = ShaderPassId::ForwardOpaque,
        .BaseColor = Vec4(0.1f, 0.85f, 0.45f, 1.0f),
    });
    scene.Blue = materials.Create(Material{
        .Pass = ShaderPassId::ForwardOpaque,
        .BaseColor = Vec4(0.2f, 0.45f, 1.0f, 1.0f),
    });

    scene.CenterCube = CreateDefaultEntity(
        registry,
        Transform3f(Vec3d(0.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
    AddDefaultMeshRenderer(registry, scene.CenterCube, scene.CubeMesh, scene.Red);

    scene.CenterCubeChild = AddCube(
        registry,
        scene.CubeMesh,
        scene.Blue,
        Vec3d(1.15f, 0.0f, 0.0f),
        Vec3d(0.35f, 0.35f, 0.35f));
    registry.Resources.Get<TransformHierarchyService>().SetParent(
        scene.CenterCubeChild,
        scene.CenterCube);

    AddCube(registry, scene.CubeMesh, scene.Green, Vec3d(-2.2f, 0.0f, -1.0f), Vec3d::One());
    AddCube(registry, scene.CubeMesh, scene.Blue, Vec3d(2.2f, 0.0f, -1.0f), Vec3d(0.75f, 1.5f, 0.75f));
    AddCube(registry, scene.CubeMesh, scene.Green, Vec3d(0.0f, -1.4f, -3.0f), Vec3d(8.0f, 0.15f, 8.0f));

    scene.Camera = CreateDefaultEntity(
        registry,
        Transform3f(Vec3d(0.0f, 1.0f, 5.0f), Quatf::Identity(), Vec3d::One()));
    AddDefaultCamera(registry, scene.Camera, CameraComponent{
        .Projection = ProjectionKind::Perspective,
        .FovYRadians = 1.22173048f,
        .NearPlane = 0.1f,
        .FarPlane = 1000.0f,
    });

    freeCamera.Entity = scene.Camera;
    return scene;
}
