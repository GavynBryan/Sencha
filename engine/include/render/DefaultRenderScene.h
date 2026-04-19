#pragma once

#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPresentationStore.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

struct DefaultRenderScene
{
    TransformHierarchyService* Hierarchy = nullptr;
    TransformPropagationOrderService* PropagationOrder = nullptr;
    TransformStore<Transform3f>* Transforms = nullptr;
    TransformPresentationStore<Transform3f>* PresentationTransforms = nullptr;
    MeshRendererStore* Renderers = nullptr;
    CameraStore* Cameras = nullptr;
    ActiveCameraService* ActiveCamera = nullptr;
    MeshService* Meshes = nullptr;
    MaterialStore* Materials = nullptr;

    [[nodiscard]] bool IsValid() const
    {
        return Hierarchy != nullptr
            && PropagationOrder != nullptr
            && Transforms != nullptr
            && PresentationTransforms != nullptr
            && Renderers != nullptr
            && Cameras != nullptr
            && ActiveCamera != nullptr
            && Meshes != nullptr
            && Materials != nullptr;
    }
};
