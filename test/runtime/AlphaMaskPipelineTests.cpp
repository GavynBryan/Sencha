// Which pipeline an alpha-masked material draws through, and what that costs
// the fields around it.
//
// MaterialAlphaMode::Mask was parsed, validated, written, round-tripped, and
// given an editor slider while rendering identically to Opaque -- there was no
// discard in any engine shader. Masking is a pipeline variant rather than a
// branch because a fragment shader that can discard gives up early depth
// testing, and an opaque scene should not pay that to serve the masked
// materials in it. These pin the selection and the two places adding a third
// axis could quietly break something else.

#include <gtest/gtest.h>

#include <render/MeshForwardPass.h>
#include <render/RenderQueue.h>

namespace
{

Material MakeMaterial(MaterialShading shading, bool doubleSided, MaterialAlphaMode alpha)
{
    Material material;
    material.Shading = shading;
    material.DoubleSided = doubleSided;
    material.AlphaMode = alpha;
    return material;
}

} // namespace

TEST(SelectOpaquePipeline, MaskingIsIndependentOfShadingAndCulling)
{
    struct Case
    {
        MaterialShading Shading;
        bool DoubleSided;
        MaterialAlphaMode Alpha;
        OpaquePipelineId Expected;
    };
    // Every combination, because the id is read back as bits by the pass: a
    // reordering that still compiles would silently build the wrong variant.
    const Case cases[] = {
        { MaterialShading::StandardLit, false, MaterialAlphaMode::Opaque,
          OpaquePipelineId::StandardLitBack },
        { MaterialShading::StandardLit, true,  MaterialAlphaMode::Opaque,
          OpaquePipelineId::StandardLitDoubleSided },
        { MaterialShading::Unlit,       false, MaterialAlphaMode::Opaque,
          OpaquePipelineId::UnlitBack },
        { MaterialShading::Unlit,       true,  MaterialAlphaMode::Opaque,
          OpaquePipelineId::UnlitDoubleSided },
        { MaterialShading::StandardLit, false, MaterialAlphaMode::Mask,
          OpaquePipelineId::StandardLitBackMasked },
        { MaterialShading::StandardLit, true,  MaterialAlphaMode::Mask,
          OpaquePipelineId::StandardLitDoubleSidedMasked },
        { MaterialShading::Unlit,       false, MaterialAlphaMode::Mask,
          OpaquePipelineId::UnlitBackMasked },
        { MaterialShading::Unlit,       true,  MaterialAlphaMode::Mask,
          OpaquePipelineId::UnlitDoubleSidedMasked },
    };

    for (const Case& c : cases)
        EXPECT_EQ(SelectOpaquePipeline(MakeMaterial(c.Shading, c.DoubleSided, c.Alpha)),
                  c.Expected);
}

TEST(SelectOpaquePipeline, BlendStillDrawsThroughTheOpaqueVariant)
{
    // There is no transparent phase yet, and the loader says so when it loads
    // one. Selecting a masked variant for it would cut fragments the author
    // asked to blend.
    EXPECT_EQ(SelectOpaquePipeline(
                  MakeMaterial(MaterialShading::StandardLit, false, MaterialAlphaMode::Blend)),
              OpaquePipelineId::StandardLitBack);
}

TEST(OpaqueSortKey, TheWidestPipelineIdStaysInsideItsOwnField)
{
    // The pipeline field sits directly below the pass field. An id wider than
    // the field carries into the pass bits, which sorts a draw into a pass it
    // does not belong to -- so the widest id in one pass must still order
    // below the narrowest id in the next.
    RenderQueueItem widest;
    widest.Pass = ShaderPassId::ForwardOpaque;
    widest.Pipeline = static_cast<OpaquePipelineId>(kOpaquePipelineCount - 1);

    RenderQueueItem nextPass = widest;
    nextPass.Pass = static_cast<ShaderPassId>(
        static_cast<std::uint8_t>(ShaderPassId::ForwardOpaque) + 1);
    nextPass.Pipeline = OpaquePipelineId::StandardLitBack;

    EXPECT_LT(BuildOpaqueSortKey(widest), BuildOpaqueSortKey(nextPass));
}

TEST(OpaqueSortKey, EveryPipelineIdOrdersDistinctlyWithinOnePass)
{
    std::uint64_t previous = 0;
    for (std::size_t id = 0; id < kOpaquePipelineCount; ++id)
    {
        RenderQueueItem item;
        item.Pipeline = static_cast<OpaquePipelineId>(id);
        const std::uint64_t key = BuildOpaqueSortKey(item);
        if (id > 0)
        {
            EXPECT_GT(key, previous) << "pipeline " << id << " did not order after " << id - 1;
        }
        previous = key;
    }
}

TEST(DebugPipelineIndex, KeepsTheCullAndMaskAxesAndDropsShading)
{
    // The debug families do not vary over lit/unlit, so the fold has to land
    // every lit/unlit pair on one index -- and must not lose masking, or a
    // debug view describes geometry the lit pass cut away.
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::StandardLitBack), 0u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::UnlitBack), 0u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::StandardLitDoubleSided), 1u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::UnlitDoubleSided), 1u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::StandardLitBackMasked), 2u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::UnlitBackMasked), 2u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::StandardLitDoubleSidedMasked), 3u);
    EXPECT_EQ(DebugPipelineIndex(OpaquePipelineId::UnlitDoubleSidedMasked), 3u);

    for (std::size_t id = 0; id < kOpaquePipelineCount; ++id)
        EXPECT_LT(DebugPipelineIndex(static_cast<OpaquePipelineId>(id)), kDebugPipelineCount);
}
