// Stage 2: per-step identities compose in dependency order, so changing one
// input moves exactly the fingerprints that mechanically depend on it.

#include "document/DocumentCookFingerprints.h"
#include "document/DocumentCookSnapshot.h"

#include <gtest/gtest.h>

namespace
{
    DocumentCookSnapshot MakeLitSnapshot()
    {
        DocumentCookSnapshot snapshot;
        snapshot.PassthroughScene = JsonValue(JsonValue::Object{});

        BakeDirectLight light{};
        light.Kind = BakeLightKind::Point;
        light.Position = Vec3d{ 1.0, 2.0, 3.0 };
        light.Intensity = 10.0f;
        light.Range = 8.0f;
        snapshot.BakeLights.push_back(light);
        return snapshot;
    }

    DocumentCookFingerprints Compute(const DocumentCookSnapshot& snapshot)
    {
        return ComputeDocumentCookFingerprints(snapshot, /*cellSize*/ 16.0, {});
    }
} // namespace

TEST(DocumentCookFingerprints, LuxelSizeMovesSurfacesNotOcclusion)
{
    const DocumentCookFingerprints base = Compute(MakeLitSnapshot());

    DocumentCookSnapshot changed = MakeLitSnapshot();
    changed.Lighting.LuxelSize += 0.1f;
    const DocumentCookFingerprints after = Compute(changed);

    EXPECT_NE(base.LightmapSurfaces, after.LightmapSurfaces);
    EXPECT_EQ(base.Occlusion, after.Occlusion);
    EXPECT_EQ(base.Brush, after.Brush);
    EXPECT_NE(base.Document, after.Document);
}

TEST(DocumentCookFingerprints, AoParamsMoveAoNotDirect)
{
    const DocumentCookFingerprints base = Compute(MakeLitSnapshot());

    DocumentCookSnapshot changed = MakeLitSnapshot();
    changed.Lighting.Ao.MaxDistance += 0.5f;
    const DocumentCookFingerprints after = Compute(changed);

    EXPECT_NE(base.Ao, after.Ao);
    EXPECT_EQ(base.Direct, after.Direct);
    EXPECT_EQ(base.LightmapSurfaces, after.LightmapSurfaces);
}

TEST(DocumentCookFingerprints, DirectLightMovesDirectNotSurfaces)
{
    const DocumentCookFingerprints base = Compute(MakeLitSnapshot());

    DocumentCookSnapshot changed = MakeLitSnapshot();
    changed.BakeLights[0].Intensity += 5.0f;
    const DocumentCookFingerprints after = Compute(changed);

    EXPECT_NE(base.Direct, after.Direct);
    EXPECT_EQ(base.LightmapSurfaces, after.LightmapSurfaces);
    EXPECT_EQ(base.Occlusion, after.Occlusion);
    // AO currently folds in the direct identity (pre-Stage-5 coupling): a
    // direct-light change still moves AO. Stage 5 separates them, at which point
    // this becomes EXPECT_EQ.
    EXPECT_NE(base.Ao, after.Ao);
}
