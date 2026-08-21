// Whether a skinned mesh grounds itself, decided by the per-instance rule the
// chunk walk applies. The flag is the projected-shadow leg of the three-flag
// participation seam; nothing else may read it.

#include <gtest/gtest.h>

#include <render/ProjectedShadowExtractionSystem.h>

namespace
{

GpuStaticMesh MakeMesh()
{
    GpuStaticMesh mesh;
    mesh.LocalBounds = Aabb3d(Vec3d(-0.5f, 0.0f, -0.5f), Vec3d(0.5f, 2.0f, 0.5f));
    return mesh;
}

} // namespace

TEST(ProjectedShadowExtraction, GroundsByDefault)
{
    // Default-on is the ratified authoring contract: a character placed with
    // no flags flipped gets a grounding shadow.
    SkinnedMeshComponent renderer;
    EXPECT_TRUE(renderer.CastsProjectedShadow);

    ProjectedShadowSet set;
    EXPECT_TRUE(AppendProjectedShadowCaster(renderer, MakeMesh(), Mat4::Identity(),
                                            RenderEntityKey{}, set));
    ASSERT_EQ(set.Casters.size(), 1u);
}

TEST(ProjectedShadowExtraction, TheFlagAndVisibilityEachVeto)
{
    ProjectedShadowSet set;

    SkinnedMeshComponent optedOut;
    optedOut.CastsProjectedShadow = false;
    EXPECT_FALSE(AppendProjectedShadowCaster(optedOut, MakeMesh(), Mat4::Identity(),
                                             RenderEntityKey{}, set));

    SkinnedMeshComponent hidden;
    hidden.Visible = false;
    EXPECT_FALSE(AppendProjectedShadowCaster(hidden, MakeMesh(), Mat4::Identity(),
                                             RenderEntityKey{}, set));

    EXPECT_TRUE(set.Casters.empty());
}

TEST(ProjectedShadowExtraction, TheRecordCarriesTheRenderedPose)
{
    SkinnedMeshComponent renderer;
    renderer.SectionMask = 0x5u;

    const Mat4 world = Mat4::MakeTranslation(3.0f, 1.0f, -2.0f);
    ProjectedShadowSet set;
    ASSERT_TRUE(AppendProjectedShadowCaster(renderer, MakeMesh(), world,
                                            RenderEntityKey{ .Entity = EntityId{ 8, 2 } },
                                            set));

    const ProjectedShadowCaster& caster = set.Casters[0];
    EXPECT_EQ(caster.Key.Entity.Index, 8u);
    EXPECT_EQ(caster.SectionMask, 0x5u);
    // Bounds are the mesh bounds at the pose, which is what the silhouette
    // fit and the receiver sweep both key from.
    EXPECT_NEAR(caster.WorldBounds.Min.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(caster.WorldBounds.Max.Y, 3.0f, 1e-5f);
    EXPECT_NEAR(caster.WorldBounds.Center().X, 3.0f, 1e-5f);
}
