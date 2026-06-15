#include "TwoTuIdentity.h"

#include <gtest/gtest.h>

TEST(TwoTuIdentity, IdentityIndependentOfInstantiationSite)
{
    // Same stable name → same id, whether resolved here or in the other unit.
    EXPECT_EQ(ResolveComponentTypeId<TwoTuComponent>().Value,
              TwoTuComponentIdFromOtherUnit());
}

TEST(TwoTuIdentity, RegisterInOneUnitLookUpInAnother)
{
    World world;

    // Registered by the OTHER translation unit...
    const ComponentId idFromOtherUnit = RegisterTwoTuComponentInOtherUnit(world);
    // ...looked up from THIS one. The dense ids must agree.
    const ComponentId idFromThisUnit = world.GetComponentId<TwoTuComponent>();
    EXPECT_EQ(idFromThisUnit, idFromOtherUnit);

    const EntityId e = world.CreateEntity();
    AddTwoTuComponentInOtherUnit(world, e, 99);  // written by the other unit

    ASSERT_NE(world.TryGet<TwoTuComponent>(e), nullptr); // read by this one
    EXPECT_EQ(world.TryGet<TwoTuComponent>(e)->Value, 99);
}
