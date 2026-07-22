#include "document/CookGraph.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    bool Contains(std::span<const std::string> values, std::string_view value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
}

TEST(CookGraphTest, LightingOnlyExpandsStructuralPrerequisites)
{
    const CookProfile& profile = BuiltInCookProfiles()[1];
    const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
    ASSERT_TRUE(graph.Valid) << graph.Error;

    EXPECT_TRUE(Contains(graph.OrderedSteps, DocumentCookStepIds::BrushCells));
    EXPECT_TRUE(Contains(graph.OrderedSteps, DocumentCookStepIds::LightmapSurfaces));
    EXPECT_TRUE(Contains(graph.OrderedSteps, DocumentCookStepIds::OcclusionGeometry));
    EXPECT_TRUE(Contains(graph.OrderedSteps, CookStepIds::RenderMeshes));
    EXPECT_TRUE(Contains(graph.OrderedSteps, CookStepIds::DirectLightmap));
    EXPECT_TRUE(Contains(graph.OrderedSteps, CookStepIds::AmbientOcclusion));
    EXPECT_TRUE(Contains(graph.OrderedSteps, CookStepIds::IrradianceProbes));
    EXPECT_FALSE(Contains(graph.OrderedSteps, CookStepIds::Collision));
    EXPECT_TRUE(Contains(graph.AddedPrerequisites, CookStepIds::RenderMeshes));
}

TEST(CookGraphTest, NoLightingWithdrawsEveryLightingOutput)
{
    const CookProfile& profile = BuiltInCookProfiles()[2];
    const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
    ASSERT_TRUE(graph.Valid) << graph.Error;

    EXPECT_FALSE(Contains(graph.OrderedSteps, CookStepIds::DirectLightmap));
    EXPECT_FALSE(Contains(graph.OrderedSteps, CookStepIds::AmbientOcclusion));
    EXPECT_FALSE(Contains(graph.OrderedSteps, CookStepIds::IrradianceProbes));
    EXPECT_EQ(CookProfileOutputDisposition(profile, CookOutputFamilies::DirectLightmap),
              CookOutputDisposition::Withdraw);
    EXPECT_EQ(CookProfileOutputDisposition(profile, CookOutputFamilies::IrradianceProbes),
              CookOutputDisposition::Withdraw);
}

TEST(CookGraphTest, RejectsUnknownStepAndOutputIds)
{
    CookProfile profile{
        .Id = "broken",
        .Name = "Broken",
        .TargetSteps = { "missing_step" },
    };
    EXPECT_FALSE(ResolveDocumentCookGraph(profile).Valid);

    profile.TargetSteps = { std::string(CookStepIds::RenderMeshes) };
    profile.OutputPolicies = {
        CookOutputPolicy{ "missing_output", CookOutputDisposition::Publish },
    };
    EXPECT_FALSE(ResolveDocumentCookGraph(profile).Valid);
}

