#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "render/EditorLightGather.h"

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/logging/LoggingProvider.h>
#include <render/Camera.h>
#include <render/LightExtractionSystem.h>
#include <ecs/StoragePartitionSet.h>
#include <render/PointLightComponent.h>
#include <world/transform/TransformComponents.h>

namespace
{
    CameraRenderData MakeCamera()
    {
        World world;
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<CameraComponent>();
        const EntityId entity = world.CreateEntity();
        world.AddComponent(entity, WorldTransform{});
        world.AddComponent(entity, CameraComponent{});
        ActiveCameraService active;
        active.SetActive(entity);
        CameraRenderData camera;
        EXPECT_TRUE(CameraRenderDataSystem::Build(
            active, world, VkExtent2D{ 1280, 720 }, camera));
        return camera;
    }
}

TEST(EditorLightGather, MatchesRuntimeSelectionBeyondForwardBudget)
{
    RegisterDocumentSerializers();
    LoggingProvider logging;
    EditorDocument document(logging);
    Registry& registry = document.GetScene().GetRegistry();
    World& world = registry.Components;
    world.RegisterComponent<WorldTransform>();

    constexpr std::uint32_t candidateCount = kMaxForwardLights + 12u;
    for (std::uint32_t index = 0; index < candidateCount; ++index)
    {
        const Vec<3> position(
            static_cast<float>(index % 8u) * 0.1f,
            static_cast<float>(index / 8u) * 0.1f,
            -5.0f);
        const EntityId entity = document.GetScene().CreateEntity(position);
        PointLightComponent light;
        light.Intensity = static_cast<float>(index + 1u);
        light.Range = 20.0f;
        light.CastShadows = (index % 3u) == 0u;
        world.AddComponent(entity, light);
        WorldTransform transform;
        transform.Value.Position = position;
        world.AddComponent(entity, transform);
    }

    const CameraRenderData camera = MakeCamera();
    EditorLightGather gathered = GatherEditorLights(document, false, 1.0f);
    RenderLightSet editorLights;
    std::vector<SpotShadowRequest> editorSpots;
    std::vector<PointShadowRequest> editorPoints;
    SelectForwardLights(gathered.Candidates, camera.Position, editorLights,
                        editorSpots, editorPoints);

    RenderLightSet runtimeLights;
    std::vector<SpotShadowRequest> runtimeSpots;
    std::vector<PointShadowRequest> runtimePoints;
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    for (EntityId entity : world.GetAliveEntities())
        partitions.Add(world.GetEntityPartition(entity));
    LightExtractionSystem extractor;
    extractor.Extract(world, partitions, camera, runtimeLights,
                      runtimeSpots, runtimePoints);

    ASSERT_EQ(editorLights.Count, kMaxForwardLights);
    ASSERT_EQ(runtimeLights.Count, editorLights.Count);
    for (std::uint32_t index = 0; index < editorLights.Count; ++index)
    {
        EXPECT_EQ(runtimeLights.Lights[index].PositionRange,
                  editorLights.Lights[index].PositionRange);
        EXPECT_EQ(runtimeLights.Lights[index].ColorIntensity,
                  editorLights.Lights[index].ColorIntensity);
    }
    ASSERT_EQ(runtimePoints.size(), editorPoints.size());
    for (std::size_t index = 0; index < editorPoints.size(); ++index)
    {
        // Editor keys carry document-registry identity; unified runtime keys
        // are entity-only. The shared selection policy must still rank the
        // same entities into the same packed slots.
        EXPECT_EQ(runtimePoints[index].Key.Entity, editorPoints[index].Key.Entity);
        EXPECT_EQ(runtimePoints[index].LightIndex, editorPoints[index].LightIndex);
    }
}
