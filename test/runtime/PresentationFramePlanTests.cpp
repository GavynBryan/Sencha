#include <gtest/gtest.h>

#include <core/handle/HandlePool.h>
#include <graphics/RenderFeatureScope.h>
#include <graphics/vulkan/PresentationFramePlan.h>
#include <runtime/RuntimeFrameLoop.h>

#include <array>
#include <vector>

// The per-frame decisions of rendering to several windows at once, pinned
// without a device: which presentations a frame takes, what a swapchain result
// means for the frame loop, and where a feature records.

namespace
{
    constexpr WindowExtent kSized{ 640, 480 };

    PresentationId Id(std::uint32_t index) { return PresentationId{ index, 1 }; }

    std::vector<PresentationId> Plan(std::initializer_list<PresentationFrameState> states)
    {
        return PlanAcquisitions(std::vector<PresentationFrameState>(states));
    }
}

TEST(PresentationFramePlan, AWindowTakesPartOnlyWhenItCanBeShown)
{
    EXPECT_EQ(ClassifyPresentation(false, kSized, false), PresentationAvailability::Acquirable);
    EXPECT_EQ(ClassifyPresentation(true, kSized, false), PresentationAvailability::Minimized);
    EXPECT_EQ(ClassifyPresentation(false, WindowExtent{ 0, 480 }, false), PresentationAvailability::ZeroExtent);
    EXPECT_EQ(ClassifyPresentation(false, kSized, true), PresentationAvailability::NeedsRebuild);
    // Minimized wins: a window that cannot be seen is not rebuilt for.
    EXPECT_EQ(ClassifyPresentation(true, kSized, true), PresentationAvailability::Minimized);
}

TEST(PresentationFramePlan, AnyOneOfThreeTargetsDropsOutWhileTheOthersAcquire)
{
    const std::array<PresentationAvailability, 3> unavailable{
        PresentationAvailability::Minimized,
        PresentationAvailability::NeedsRebuild,
        PresentationAvailability::ZeroExtent,
    };
    for (std::uint32_t out = 1; out <= 3; ++out)
    {
        for (const PresentationAvailability reason : unavailable)
        {
            std::vector<PresentationFrameState> states;
            std::vector<PresentationId> expected;
            for (std::uint32_t i = 1; i <= 3; ++i)
            {
                states.push_back({ Id(i), i == out ? reason : PresentationAvailability::Acquirable });
                if (i != out)
                    expected.push_back(Id(i));
            }
            EXPECT_EQ(PlanAcquisitions(states), expected) << "target " << out << " out";
        }
    }
}

TEST(PresentationFramePlan, AMinimizedPrimaryLeavesTheSecondariesLive)
{
    EXPECT_EQ(Plan({ { Id(1), PresentationAvailability::Minimized },
                     { Id(2), PresentationAvailability::Acquirable },
                     { Id(3), PresentationAvailability::Acquirable } }),
              (std::vector<PresentationId>{ Id(2), Id(3) }));

    // The frame loop agrees: another live presentation keeps the frame
    // rendering and simulating.
    RuntimeFrameLoop loop;
    loop.BeginFrame();
    loop.NotifyMinimized();
    loop.SetOtherPresentationLive(true);
    loop.ResolveLifecycleTransitions();
    EXPECT_FALSE(loop.GetCurrentFrame().LifecycleOnly);
    EXPECT_EQ(loop.GetState(), RuntimeFrameState::Minimized);

    loop.BeginFrame();
    loop.SetOtherPresentationLive(false);
    loop.ResolveLifecycleTransitions();
    EXPECT_TRUE(loop.GetCurrentFrame().LifecycleOnly) << "with nothing left to show, it is a minimized game";
}

TEST(PresentationFramePlan, ASurfaceResultBelongsToItsOwnTarget)
{
    EXPECT_EQ(ClassifySurfaceResult(VK_SUCCESS), SurfaceOutcome::Ok);
    EXPECT_EQ(ClassifySurfaceResult(VK_SUBOPTIMAL_KHR), SurfaceOutcome::Suboptimal);
    EXPECT_EQ(ClassifySurfaceResult(VK_ERROR_OUT_OF_DATE_KHR), SurfaceOutcome::OutOfDate);
    EXPECT_EQ(ClassifySurfaceResult(VK_ERROR_DEVICE_LOST), SurfaceOutcome::DeviceLost);
    EXPECT_EQ(ClassifySurfaceResult(VK_ERROR_SURFACE_LOST_KHR), SurfaceOutcome::Failed);
    EXPECT_TRUE(IsFatal(SurfaceOutcome::DeviceLost));
    EXPECT_FALSE(IsFatal(SurfaceOutcome::OutOfDate));
}

TEST(PresentationFramePlan, TheFrameLoopHearsThePrimaryOnly)
{
    // A secondary going out of date recorded and presented the others; the
    // primary's outcome decides what the loop hears.
    EXPECT_EQ(SummarizeFrame(SurfaceOutcome::Ok, 3), RenderFrameResult::Presented);
    EXPECT_EQ(SummarizeFrame(SurfaceOutcome::OutOfDate, 2), RenderFrameResult::SwapchainOutOfDate);
    EXPECT_EQ(SummarizeFrame(SurfaceOutcome::Suboptimal, 1), RenderFrameResult::SurfaceSuboptimal);
    EXPECT_EQ(SummarizeFrame(SurfaceOutcome::Ok, 0), RenderFrameResult::SkippedMinimized);
    EXPECT_EQ(SummarizeFrame(SurfaceOutcome::DeviceLost, 3), RenderFrameResult::Failed);
}

TEST(PresentationFramePlan, DestroyingTheMiddleTargetLeavesTheOthersAndStalesItsId)
{
    HandlePool<PresentationIdTag, int> presentations;
    const PresentationId a = presentations.Emplace(1);
    const PresentationId b = presentations.Emplace(2);
    const PresentationId c = presentations.Emplace(3);
    ASSERT_TRUE(presentations.Erase(b));

    std::vector<PresentationFrameState> states;
    presentations.ForEach([&](PresentationId id, int) { states.push_back({ id, PresentationAvailability::Acquirable }); });
    EXPECT_EQ(PlanAcquisitions(states), (std::vector<PresentationId>{ a, c }));

    const PresentationId d = presentations.Emplace(4);
    EXPECT_EQ(d.Index, b.Index) << "the slot is reused";
    EXPECT_FALSE(presentations.Contains(b)) << "but the old id never names its new occupant";
    EXPECT_EQ(*presentations.Find(d), 4);
}

TEST(RenderFeatureScope, TheDefaultFollowsThePhase)
{
    const PresentationId primary = Id(1);
    RenderFeatureScope resolved;
    ASSERT_EQ(ResolveFeatureScope(RenderPhase::MainColor, {}, primary, false, resolved), FeatureScopeFault::None);
    EXPECT_EQ(resolved, RenderFeatureScope::For(primary));
    ASSERT_EQ(ResolveFeatureScope(RenderPhase::Offscreen, {}, primary, false, resolved), FeatureScopeFault::None);
    EXPECT_EQ(resolved, RenderFeatureScope::Global());
}

TEST(RenderFeatureScope, ASwapchainPhaseRecordsIntoAPresentation)
{
    RenderFeatureScope resolved;
    EXPECT_EQ(ResolveFeatureScope(RenderPhase::ApplicationUi, RenderFeatureScope::Global(), Id(1), false, resolved),
              FeatureScopeFault::SwapchainPhaseNeedsPresentation);
    EXPECT_EQ(ResolveFeatureScope(RenderPhase::MainColor, RenderFeatureScope::For(Id(2)), Id(1), true, resolved),
              FeatureScopeFault::None);
    EXPECT_EQ(resolved, RenderFeatureScope::For(Id(2)));
}

TEST(RenderFeatureScope, AStaleOrUnknownPresentationIsRefused)
{
    RenderFeatureScope resolved;
    EXPECT_EQ(ResolveFeatureScope(RenderPhase::MainColor, RenderFeatureScope::For(Id(9)), Id(1), false, resolved),
              FeatureScopeFault::UnknownPresentation);
    EXPECT_EQ(ResolveFeatureScope(RenderPhase::Offscreen, RenderFeatureScope::For(Id(9)), Id(1), false, resolved),
              FeatureScopeFault::UnknownPresentation);
}

TEST(RenderFeatureScope, APresentationIsBoundWhileAFeatureRecordsIntoIt)
{
    const std::vector<RenderFeatureScope> scopes{
        RenderFeatureScope::Global(),
        RenderFeatureScope::For(Id(1)),
        RenderFeatureScope::For(Id(3)),
    };
    EXPECT_TRUE(IsPresentationBound(scopes, Id(1)));
    EXPECT_FALSE(IsPresentationBound(scopes, Id(2)));
    EXPECT_TRUE(IsPresentationBound(scopes, Id(3)));
    EXPECT_FALSE(IsPresentationBound(scopes, PresentationId{ 3, 2 })) << "a later generation of the slot is not bound";
}
