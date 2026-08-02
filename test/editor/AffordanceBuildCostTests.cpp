#include "WorkspaceFixture.h"

#include "authoring/EditorComponentAdapter.h"

#include <world/serialization/SceneSerializer.h>

#include <chrono>
#include <cstdio>
#include "document/DocumentSerialization.h"

// Viewport affordances are rebuilt several times a frame (each viewport, the
// overlay, hover, hit-testing), so the cost of one build is multiplied by a
// number nobody controls from the call site. These bound that cost in terms of
// what actually authors affordances rather than what happens to be registered.
namespace
{

class AffordanceBuildCostTest : public WorkspaceTest
{
protected:
    void AddBrushes(int count)
    {
        for (int i = 0; i < count; ++i)
            AddBrush(Vec3d{ static_cast<float>(i) * 2.0f, 0, 0 });
    }

    [[nodiscard]] EditorAffordanceService& Affordances() { return *Workspace.Affordances; }
};

TEST_F(AffordanceBuildCostTest, WorkScalesWithAdaptersNotRegisteredComponentTypes)
{
    // One adapter is registered (the world dock) against every serializable
    // component type. Walking the registered types per entity would make the
    // inner loop an order of magnitude wider than the work that can result.
    const std::size_t adapters = Affordances().Registry().Entries().size();
    const std::size_t serializers = EditorSceneSerializers().Entries().size();
    EXPECT_GT(serializers, adapters);
    EXPECT_LE(adapters, 4u) << "affordance adapters grew; re-check the per-entity inner loop";
}

TEST_F(AffordanceBuildCostTest, BuildingIsProportionalToVisibleEntities)
{
    AddBrushes(500);

    ViewportAffordanceOutput output;
    const auto start = std::chrono::steady_clock::now();
    constexpr int kBuilds = 200;
    for (int i = 0; i < kBuilds; ++i)
    {
        output = {};
        Affordances().Build(output);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double usPerBuild =
        std::chrono::duration<double, std::micro>(elapsed).count() / kBuilds;
    std::printf("[affordance] %.1f us/build over 500 entities\n", usPerBuild);

    // Generous: this is a bound against the shape regressing (a per-entity walk
    // of every registered component type), not a tuned target.
    EXPECT_LT(usPerBuild, 500.0);
}

TEST_F(AffordanceBuildCostTest, HasEditTargetsIsCountedLikeAnyOtherBuild)
{
    // It answers a yes/no question by building the full output, so it costs a
    // build; the counter makes that visible rather than hidden behind a bool.
    const std::uint64_t before = Affordances().BuildCount();
    (void)Affordances().HasEditTargets();
    EXPECT_EQ(Affordances().BuildCount(), before + 1);
}

}
