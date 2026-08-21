// Which list a blended draw lands in, and what order it draws in.
//
// alpha_mode blend was parsed, validated, round-tripped, and given an editor
// field while rendering opaque -- the loader even warned about it on every
// load. The pass is classified at material load now, EmitMeshSections routes
// on it, and the order is decided per view because a host may replay one queue
// under several cameras: back-to-front is a property of the camera, and
// blending makes it a correctness property, not a state optimization.

#include <gtest/gtest.h>

#include <render/MeshDrawInstance.h>
#include <render/MeshForwardPass.h>
#include <render/RenderQueue.h>

namespace
{

GpuStaticMesh MakeMesh()
{
    GpuStaticMesh mesh;
    StaticMeshSection section;
    section.MaterialSlot = 0;
    section.IndexCount = 3;
    mesh.Sections.push_back(section);
    return mesh;
}

Material MakeMaterial(ShaderPassId pass)
{
    Material material;
    material.Pass = pass;
    if (pass == ShaderPassId::ForwardTransparent)
        material.AlphaMode = MaterialAlphaMode::Blend;
    return material;
}

RenderQueueItem ItemAt(float x, float y, float z)
{
    RenderQueueItem item;
    item.WorldBounds = Aabb3d::FromCenterHalfExtent(Vec3d(x, y, z), Vec3d(0.5f, 0.5f, 0.5f));
    return item;
}

} // namespace

TEST(TransparentRouting, ABlendClassifiedMaterialLandsInTheTransparentList)
{
    MaterialCache materials;
    const MaterialHandle blend =
        materials.Create(MakeMaterial(ShaderPassId::ForwardTransparent));
    const GpuStaticMesh mesh = MakeMesh();
    const MaterialHandle slots[] = { blend };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 1u);

    EXPECT_TRUE(queue.Opaque().empty());
    ASSERT_EQ(queue.Transparent().size(), 1u);
    EXPECT_EQ(queue.Transparent()[0].Pass, ShaderPassId::ForwardTransparent)
        << "the item must carry the pass it was routed by, or the sort key and "
           "stats misreport it";
}

TEST(TransparentRouting, AnOpaqueClassifiedMaterialStaysOutOfIt)
{
    MaterialCache materials;
    const MaterialHandle opaque = materials.Create(MakeMaterial(ShaderPassId::ForwardOpaque));
    const GpuStaticMesh mesh = MakeMesh();
    const MaterialHandle slots[] = { opaque };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 1u);

    EXPECT_TRUE(queue.Transparent().empty());
    EXPECT_EQ(queue.Opaque().size(), 1u);
}

TEST(TransparentRouting, ResetForgetsTheTransparentList)
{
    RenderQueue queue;
    queue.AddTransparent(ItemAt(0.0f, 0.0f, 0.0f));
    queue.Reset();
    EXPECT_TRUE(queue.Transparent().empty());
}

TEST(TransparentOrder, DrawsTheFarthestItemFirst)
{
    const RenderQueueItem items[] = {
        ItemAt(0.0f, 0.0f, 1.0f),  // nearest to the camera below
        ItemAt(0.0f, 0.0f, 9.0f),  // farthest
        ItemAt(0.0f, 0.0f, 5.0f),
    };

    std::vector<uint32_t> order;
    BuildTransparentOrder(items, Vec3d(0.0f, 0.0f, 0.0f), order);

    EXPECT_EQ(order, (std::vector<uint32_t>{ 1, 2, 0 }));
}

TEST(TransparentOrder, TheOrderBelongsToTheViewNotTheQueue)
{
    // The same two items from cameras on opposite sides: each view must put
    // the other item first. This is why the order is computed per Draw call
    // rather than baked at extraction -- the editor replays one queue under
    // four cameras.
    const RenderQueueItem items[] = {
        ItemAt(0.0f, 0.0f, 0.0f),
        ItemAt(0.0f, 0.0f, 10.0f),
    };

    std::vector<uint32_t> fromBelow;
    BuildTransparentOrder(items, Vec3d(0.0f, 0.0f, -5.0f), fromBelow);
    std::vector<uint32_t> fromAbove;
    BuildTransparentOrder(items, Vec3d(0.0f, 0.0f, 15.0f), fromAbove);

    EXPECT_EQ(fromBelow, (std::vector<uint32_t>{ 1, 0 }));
    EXPECT_EQ(fromAbove, (std::vector<uint32_t>{ 0, 1 }));
}

TEST(TransparentOrder, EquidistantItemsKeepTheirQueueOrder)
{
    // Two items at the same distance have no correct blend order; what they
    // must have is the same order every frame, or the image shimmers.
    const RenderQueueItem items[] = {
        ItemAt(3.0f, 0.0f, 0.0f),
        ItemAt(-3.0f, 0.0f, 0.0f),
        ItemAt(0.0f, 3.0f, 0.0f),
    };

    std::vector<uint32_t> order;
    BuildTransparentOrder(items, Vec3d(0.0f, 0.0f, 0.0f), order);

    EXPECT_EQ(order, (std::vector<uint32_t>{ 0, 1, 2 }));
}

TEST(TransparentOrder, ReusesTheCallersScratchWithoutGrowingIt)
{
    const RenderQueueItem items[] = { ItemAt(0.0f, 0.0f, 1.0f) };

    // A previous, larger frame left capacity behind; this frame must clear the
    // stale entries and reuse the storage.
    std::vector<uint32_t> order{ 7, 8, 9 };
    BuildTransparentOrder(items, Vec3d(0.0f, 0.0f, 0.0f), order);

    EXPECT_EQ(order, (std::vector<uint32_t>{ 0 }));
}

TEST(TransparentPipelineIndex, KeepsCullAndShadingAndNeverTheMaskBit)
{
    EXPECT_EQ(TransparentPipelineIndex(OpaquePipelineId::StandardLitBack), 0u);
    EXPECT_EQ(TransparentPipelineIndex(OpaquePipelineId::StandardLitDoubleSided), 1u);
    EXPECT_EQ(TransparentPipelineIndex(OpaquePipelineId::UnlitBack), 2u);
    EXPECT_EQ(TransparentPipelineIndex(OpaquePipelineId::UnlitDoubleSided), 3u);
    for (std::size_t id = 0; id < kOpaquePipelineCount; ++id)
        EXPECT_LT(TransparentPipelineIndex(static_cast<OpaquePipelineId>(id)),
                  kTransparentPipelineCount);
}
