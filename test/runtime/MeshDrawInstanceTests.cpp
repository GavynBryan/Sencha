// The one expansion from a resolved instance to per-section queue items.
//
// Four loops did this independently -- the engine's ECS walk and the editor's
// brush, placed-mesh, and cooked-preview builders. Three of the four never set
// Pass or Pipeline, so an unlit material rendered lit and a double-sided
// material rendered back-face culled in the editor viewport but not in game.
// That class of drift is what these tests exist to prevent: the fields are
// asserted here, once, for whoever calls it.

#include <gtest/gtest.h>

#include <render/MaterialCache.h>
#include <render/MeshDrawInstance.h>
#include <render/RenderQueue.h>

namespace
{

GpuStaticMesh MakeMesh(std::initializer_list<std::uint32_t> materialSlots)
{
    GpuStaticMesh mesh;
    for (const std::uint32_t slot : materialSlots)
    {
        StaticMeshSection section;
        section.MaterialSlot = slot;
        section.IndexCount = 3;
        mesh.Sections.push_back(section);
    }
    return mesh;
}

Material MakeMaterial(MaterialShading shading, bool doubleSided)
{
    Material material;
    material.Shading = shading;
    material.DoubleSided = doubleSided;
    return material;
}

} // namespace

TEST(EmitMeshSections, EmitsOneItemPerSection)
{
    MaterialCache materials;
    const MaterialHandle lit =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0, 1, 2 });
    const MaterialHandle slots[] = { lit, lit, lit };

    RenderQueue queue;
    MeshDrawInstance instance;
    EXPECT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 3u);
    EXPECT_EQ(queue.Opaque().size(), 3u);
}

TEST(EmitMeshSections, SkipsSectionsTheMaskClears)
{
    MaterialCache materials;
    const MaterialHandle lit =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0, 0, 0, 0 });
    const MaterialHandle slots[] = { lit };

    RenderQueue queue;
    MeshDrawInstance instance;
    instance.SectionMask = 0b1010;

    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 2u);
    EXPECT_EQ(queue.Opaque()[0].SectionIndex, 1u);
    EXPECT_EQ(queue.Opaque()[1].SectionIndex, 3u);
}

TEST(EmitMeshSections, ResolvesEachSectionThroughItsMaterialSlot)
{
    MaterialCache materials;
    const MaterialHandle first =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const MaterialHandle second =
        materials.Create(MakeMaterial(MaterialShading::Unlit, false));
    // Section 0 uses slot 1 and section 1 uses slot 0: the mapping is the
    // section's MaterialSlot, never its index.
    const GpuStaticMesh mesh = MakeMesh({ 1, 0 });
    const MaterialHandle slots[] = { first, second };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 2u);
    EXPECT_EQ(queue.Opaque()[0].Material, second);
    EXPECT_EQ(queue.Opaque()[1].Material, first);
}

TEST(EmitMeshSections, FallsBackToTheLastMaterialForAnUnderBoundSet)
{
    MaterialCache materials;
    const MaterialHandle only =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0, 7 });
    const MaterialHandle slots[] = { only };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 2u);
    EXPECT_EQ(queue.Opaque()[1].Material, only);
}

TEST(EmitMeshSections, EmitsNothingWithoutMaterials)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeMesh({ 0 });

    RenderQueue queue;
    MeshDrawInstance instance;
    EXPECT_EQ(EmitMeshSections(instance, mesh, {}, materials, queue), 0u);
    EXPECT_TRUE(queue.Opaque().empty());
}

TEST(EmitMeshSections, SkipsASectionWhoseMaterialIsNotResident)
{
    MaterialCache materials;
    const GpuStaticMesh mesh = MakeMesh({ 0 });
    const MaterialHandle stale{};  // never created

    RenderQueue queue;
    MeshDrawInstance instance;
    // The pass drops such an item at record time anyway, and Pass and Pipeline
    // cannot be resolved without the material.
    EXPECT_EQ(EmitMeshSections(instance, mesh, { &stale, 1 }, materials, queue), 0u);
}

// --- the fields three of the four callers used to forget ---
//
// Pipeline is the live defect: it selects the shader variant, so a default one
// draws an unlit material lit. Pass is copied too but cannot be asserted here
// yet -- ShaderPassId has a single value today, and gains a second when P6
// adds the transparent pass, which is when a forgotten copy would start to
// matter.

TEST(EmitMeshSections, TakesPipelineFromTheMaterialShading)
{
    MaterialCache materials;
    const MaterialHandle unlit =
        materials.Create(MakeMaterial(MaterialShading::Unlit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0 });
    const MaterialHandle slots[] = { unlit };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 1u);
    EXPECT_EQ(queue.Opaque()[0].Pipeline, OpaquePipelineId::UnlitBack)
        << "an unlit material left at the default pipeline renders lit";
}

TEST(EmitMeshSections, TakesPipelineFromDoubleSidedness)
{
    MaterialCache materials;
    const MaterialHandle twoSided =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, true));
    const GpuStaticMesh mesh = MakeMesh({ 0 });
    const MaterialHandle slots[] = { twoSided };

    RenderQueue queue;
    MeshDrawInstance instance;
    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 1u);
    EXPECT_EQ(queue.Opaque()[0].Pipeline,
              OpaquePipelineId::StandardLitDoubleSided)
        << "a double-sided material left at the default pipeline is back-face culled";
}

TEST(EmitMeshSections, CopiesTheInstanceFieldsOntoEverySection)
{
    MaterialCache materials;
    const MaterialHandle lit =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0, 0 });
    const MaterialHandle slots[] = { lit };

    RenderQueue queue;
    MeshDrawInstance instance;
    instance.WorldMatrix = Mat4::MakeTranslation(Vec3d(1.0f, 2.0f, 3.0f));
    instance.WorldBounds = Aabb3d::FromMinMax(Vec3d(-1.0f, -1.0f, -1.0f),
                                              Vec3d(1.0f, 1.0f, 1.0f));
    instance.CameraDepth = 12.5f;
    instance.LightmapTextureIndex = 7;
    instance.AoTextureIndex = 9;
    instance.LightmapScaleBias = Vec4{ 0.5f, 0.5f, 0.25f, 0.25f };

    ASSERT_EQ(EmitMeshSections(instance, mesh, slots, materials, queue), 2u);
    for (const RenderQueueItem& item : queue.Opaque())
    {
        EXPECT_EQ(item.WorldMatrix, instance.WorldMatrix);
        EXPECT_EQ(item.WorldBounds, instance.WorldBounds);
        EXPECT_FLOAT_EQ(item.CameraDepth, 12.5f);
        EXPECT_EQ(item.LightmapTextureIndex, 7u);
        EXPECT_EQ(item.AoTextureIndex, 9u);
        EXPECT_FLOAT_EQ(item.LightmapScaleBias.X, 0.5f);
        EXPECT_FLOAT_EQ(item.LightmapScaleBias.Z, 0.25f);
    }
}

TEST(EmitMeshSections, AppendsRatherThanReplacing)
{
    // Hosts emit many instances into one queue; the brush builder emits one
    // section at a time because brush section bounds are already world-space.
    MaterialCache materials;
    const MaterialHandle lit =
        materials.Create(MakeMaterial(MaterialShading::StandardLit, false));
    const GpuStaticMesh mesh = MakeMesh({ 0 });
    const MaterialHandle slots[] = { lit };

    RenderQueue queue;
    MeshDrawInstance instance;
    EmitMeshSections(instance, mesh, slots, materials, queue);
    EmitMeshSections(instance, mesh, slots, materials, queue);
    EXPECT_EQ(queue.Opaque().size(), 2u);
}
