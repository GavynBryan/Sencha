#include <gtest/gtest.h>

#include <graphics/vulkan/FeatureRegistrationOrder.h>

#include <array>
#include <string_view>
#include <vector>

// Feature order used to be whatever order a host called AddFeature in, with the
// real constraints written as comments above the calls. These pin the resolver
// that replaced them: the same declarations must produce the same order however
// the host staged them, and a graph that cannot be trusted must refuse rather
// than register most of itself.

namespace
{
struct Resolved
{
    bool Ok = false;
    std::vector<std::size_t> Order;
    std::vector<FeatureOrderProblem> Problems;
};

Resolved Resolve(std::span<const FeatureRegistration> staged,
                 std::span<const std::string_view> registered = {})
{
    Resolved out;
    out.Ok = ResolveFeatureOrder(staged, registered, out.Order, out.Problems);
    return out;
}

std::vector<std::string_view> IdsInOrder(std::span<const FeatureRegistration> staged,
                                         const Resolved& resolved)
{
    std::vector<std::string_view> ids;
    for (const std::size_t index : resolved.Order)
        ids.push_back(staged[index].Id);
    return ids;
}

constexpr std::array<std::string_view, 2> kOnShadowAndSky{ "shadow", "sky" };
} // namespace

TEST(FeatureRegistrationOrder, StagingOrderDoesNotDecideResolvedOrder)
{
    // The runtime's real shape, staged consumer-first -- the order that used to
    // be wrong and silent.
    const FeatureRegistration staged[] = {
        { .Id = "mesh", .DependsOn = kOnShadowAndSky },
        { .Id = "sky" },
        { .Id = "shadow" },
    };
    const Resolved resolved = Resolve(staged);

    ASSERT_TRUE(resolved.Ok);
    const std::vector<std::string_view> ids = IdsInOrder(staged, resolved);
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids.back(), "mesh");
    // Both producers precede their consumer; between themselves they declared
    // nothing, so the host's staging order decides.
    EXPECT_EQ(ids[0], "sky");
    EXPECT_EQ(ids[1], "shadow");
}

TEST(FeatureRegistrationOrder, IndependentFeaturesKeepStagingOrder)
{
    const FeatureRegistration staged[] = {
        { .Id = "first" }, { .Id = "second" }, { .Id = "third" },
    };
    const Resolved resolved = Resolve(staged);

    ASSERT_TRUE(resolved.Ok);
    EXPECT_EQ(IdsInOrder(staged, resolved),
              (std::vector<std::string_view>{ "first", "second", "third" }));
}

TEST(FeatureRegistrationOrder, AChainResolvesEndToEnd)
{
    constexpr std::array<std::string_view, 1> onA{ "a" };
    constexpr std::array<std::string_view, 1> onB{ "b" };
    const FeatureRegistration staged[] = {
        { .Id = "c", .DependsOn = onB },
        { .Id = "b", .DependsOn = onA },
        { .Id = "a" },
    };
    const Resolved resolved = Resolve(staged);

    ASSERT_TRUE(resolved.Ok);
    EXPECT_EQ(IdsInOrder(staged, resolved),
              (std::vector<std::string_view>{ "a", "b", "c" }));
}

TEST(FeatureRegistrationOrder, ADependencyCommittedEarlierResolves)
{
    // The editor stages across two build steps; a later batch may depend on
    // something already registered, which does not move.
    constexpr std::array<std::string_view, 1> onUi{ "editor_ui" };
    const FeatureRegistration staged[] = {
        { .Id = "editor_render", .DependsOn = onUi },
    };
    const std::string_view registered[] = { "editor_ui" };

    EXPECT_TRUE(Resolve(staged, registered).Ok);
}

TEST(FeatureRegistrationOrder, AnUnknownDependencyRefusesTheBatch)
{
    constexpr std::array<std::string_view, 1> onMissing{ "never_staged" };
    const FeatureRegistration staged[] = {
        { .Id = "mesh", .DependsOn = onMissing },
    };
    const Resolved resolved = Resolve(staged);

    EXPECT_FALSE(resolved.Ok);
    EXPECT_TRUE(resolved.Order.empty());
    ASSERT_EQ(resolved.Problems.size(), 1u);
    EXPECT_EQ(resolved.Problems[0].Fault, FeatureOrderFault::UnknownDependency);
    EXPECT_EQ(resolved.Problems[0].Id, "mesh");
    EXPECT_EQ(resolved.Problems[0].Dependency, "never_staged");
}

TEST(FeatureRegistrationOrder, ADuplicateIdRefusesTheBatch)
{
    const FeatureRegistration staged[] = { { .Id = "mesh" }, { .Id = "mesh" } };
    const Resolved resolved = Resolve(staged);

    EXPECT_FALSE(resolved.Ok);
    EXPECT_FALSE(resolved.Problems.empty());
    EXPECT_EQ(resolved.Problems[0].Fault, FeatureOrderFault::DuplicateId);
}

TEST(FeatureRegistrationOrder, AnIdAlreadyRegisteredIsADuplicate)
{
    const FeatureRegistration staged[] = { { .Id = "editor_ui" } };
    const std::string_view registered[] = { "editor_ui" };
    const Resolved resolved = Resolve(staged, registered);

    EXPECT_FALSE(resolved.Ok);
    ASSERT_FALSE(resolved.Problems.empty());
    EXPECT_EQ(resolved.Problems[0].Fault, FeatureOrderFault::DuplicateId);
}

TEST(FeatureRegistrationOrder, ACycleRefusesTheBatchAndNamesItsMembers)
{
    constexpr std::array<std::string_view, 1> onB{ "b" };
    constexpr std::array<std::string_view, 1> onA{ "a" };
    const FeatureRegistration staged[] = {
        { .Id = "a", .DependsOn = onB },
        { .Id = "b", .DependsOn = onA },
    };
    const Resolved resolved = Resolve(staged);

    EXPECT_FALSE(resolved.Ok);
    EXPECT_TRUE(resolved.Order.empty());
    EXPECT_EQ(resolved.Problems.size(), 2u);
    for (const FeatureOrderProblem& problem : resolved.Problems)
        EXPECT_EQ(problem.Fault, FeatureOrderFault::Cycle);
}

TEST(FeatureRegistrationOrder, AnEmptyBatchResolves)
{
    const Resolved resolved = Resolve({});
    EXPECT_TRUE(resolved.Ok);
    EXPECT_TRUE(resolved.Order.empty());
}

TEST(FeatureDependents, RemovalFindsWhoWouldBeOrphaned)
{
    constexpr std::array<std::string_view, 1> onUi{ "editor_ui" };
    const FeatureRegistration registered[] = {
        { .Id = "editor_ui" },
        { .Id = "editor_render", .DependsOn = onUi },
    };

    EXPECT_EQ(FindDependent(registered, "editor_ui"), "editor_render");
    EXPECT_TRUE(FindDependent(registered, "editor_render").empty());
}
