#include "NavigationFixture.h"

#include <core/json/JsonParser.h>
#include <navigation/NavigationPolicyData.h>

#include <gtest/gtest.h>

#include <array>

TEST(NavigationPolicyData, CompilesAndBindsToRegisteredTags)
{
    const std::optional<JsonValue> data = JsonParse(R"({
        "area_costs": [ { "area": "navigation.area.water", "cost": 1.4 },
                        { "area": "navigation.area.lava", "cost": 9 } ],
        "forbidden_areas": [ "navigation.area.hazard" ],
        "traversal_costs": [ { "kind": "navigation.traversal.jump", "multiplier": 2, "add": 1 } ] })");
    ASSERT_TRUE(data.has_value());
    const DataAssetCompileResult compiled = CompileNavigationPolicy(*data);
    ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
    const auto* policy = static_cast<const NavigationPolicyData*>(compiled.Value.get());
    ASSERT_EQ(policy->AreaCosts.size(), 2u);
    EXPECT_EQ(policy->AreaCosts[0].Area, "navigation.area.water");

    NavZoneFixture f;
    std::vector<std::string> unresolved;
    const NavQueryPolicy bound = BindNavigationPolicy(*policy, f.Tags, &unresolved);
    ASSERT_EQ(bound.AreaCosts.size(), 1u);
    EXPECT_EQ(bound.AreaCosts[0].Area, f.Water);
    EXPECT_FLOAT_EQ(bound.AreaCosts[0].Cost, 1.4f);
    ASSERT_EQ(bound.TraversalCosts.size(), 1u);
    EXPECT_EQ(bound.TraversalCosts[0].Kind, f.Jump);
    EXPECT_FLOAT_EQ(bound.TraversalCosts[0].Multiplier, 2.0f);
    // Names no code registered are reported, never guessed.
    EXPECT_EQ(unresolved, (std::vector<std::string>{ "navigation.area.lava",
                                                     "navigation.area.hazard" }));
}

TEST(NavigationPolicyData, RejectsInvalidValues)
{
    for (const char* text : {
             R"({ "area_costs": [ { "area": "navigation.area.water", "cost": 0 } ] })",
             R"({ "area_costs": [ 1 ] })",
             R"({ "area_costs": [ { "cost": 2 } ] })",
             R"({ "forbidden_areas": [ 3 ] })",
             R"({ "traversal_costs": [ { "kind": "navigation.traversal.jump", "multiplier": -1 } ] })",
             R"({ "unknown": 1 })" })
    {
        const std::optional<JsonValue> data = JsonParse(text);
        ASSERT_TRUE(data.has_value());
        EXPECT_FALSE(CompileNavigationPolicy(*data).IsValid()) << text;
    }
}

// A traversal-kind multiplier from policy data prices one link above another.
TEST(NavigationPolicyData, TraversalCostsSteerLinkChoice)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 8.0f, 10.0f);
    g.AddFloor(12.0f, 0.0f, 20.0f, 10.0f);
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(
        g, { NavZoneFixture::Link(0xA, "navigation.traversal.jump", Vec3d(7, 0, 2),
                                  Vec3d(13, 0, 2)),
             NavZoneFixture::Link(0xB, "navigation.traversal.drop", Vec3d(7, 0, 8),
                                  Vec3d(13, 0, 8)) });
    NavQueryContext context;
    const std::array capabilities{ f.Jump, f.Drop };

    const auto crossing = [&](const NavQueryPolicy* policy)
    {
        const NavQueryRequest request = f.Request(capabilities, policy);
        const NavLocation a = f.Project(zone, context, request, Vec3d(2, 0, 2));
        const NavLocation b = f.Project(zone, context, request, Vec3d(18, 0, 2));
        NavRouteBuffer route;
        EXPECT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::Success);
        for (const NavRouteStep& step : route.Steps())
            if (step.Kind == NavRouteStepKind::Traverse)
                return step.Link;
        return NavLinkId{};
    };
    EXPECT_EQ(crossing(nullptr), NavLinkId{ 0xA });

    NavigationPolicyData data;
    data.TraversalCosts.push_back({ "navigation.traversal.jump", 1.0f, 50.0f });
    const NavQueryPolicy dislikesJumping = BindNavigationPolicy(data, f.Tags);
    EXPECT_EQ(crossing(&dislikesJumping), NavLinkId{ 0xB });
}
