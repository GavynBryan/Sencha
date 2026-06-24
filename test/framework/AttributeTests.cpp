#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeResolve.h>
#include <framework/attributes/AttributeSet.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

TEST(AttributeRegistry, RegistersFindsAndClamps)
{
    AttributeRegistry reg;
    const AttributeId health = reg.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
    EXPECT_TRUE(health.IsValid());
    EXPECT_EQ(reg.FindAttribute("Health").Value, health.Value);

    // re-register is idempotent (first definition wins)
    EXPECT_EQ(reg.RegisterAttribute("Health", 0.0f, 9.0f).Value, health.Value);
    EXPECT_FLOAT_EQ(reg.Max(health), 100.0f);

    EXPECT_FLOAT_EQ(reg.Clamp(health, 150.0f), 100.0f);
    EXPECT_FLOAT_EQ(reg.Clamp(health, -10.0f), 0.0f);
    EXPECT_FLOAT_EQ(reg.Clamp(health, 42.0f), 42.0f);
    EXPECT_FLOAT_EQ(reg.DefaultBase(health), 100.0f);

    EXPECT_FALSE(reg.FindAttribute("Missing").IsValid());
    EXPECT_FALSE(reg.RegisterAttribute("").IsValid());
}

TEST(AttributeSet, AddSetGetStaysSorted)
{
    AttributeRegistry reg;
    const AttributeId hp  = reg.RegisterAttribute("Health");
    const AttributeId sta = reg.RegisterAttribute("Stamina");
    const AttributeId mp  = reg.RegisterAttribute("Mana");

    AttributeSet s{};
    EXPECT_TRUE(s.Add(mp, 50.0f));   // inserted out of id order
    EXPECT_TRUE(s.Add(hp, 80.0f));
    EXPECT_TRUE(s.Add(sta, 30.0f));
    EXPECT_FALSE(s.Add(hp, 1.0f));   // already present

    EXPECT_EQ(s.Size(), 3u);
    EXPECT_FLOAT_EQ(s.GetBase(hp), 80.0f);
    EXPECT_TRUE(s.SetBase(hp, 70.0f));
    EXPECT_FLOAT_EQ(s.GetBase(hp), 70.0f);
    EXPECT_FALSE(s.SetBase(reg.RegisterAttribute("Poise"), 1.0f)); // not in the set
    EXPECT_FLOAT_EQ(s.GetBase(reg.FindAttribute("Poise"), -1.0f), -1.0f); // fallback

    for (int i = 1; i < s.Count; ++i)
        EXPECT_LT(s.Ids[i - 1].Value, s.Ids[i].Value);
}

TEST(AttributeResolve, ClampsCurrentFromBaseViaRegistry)
{
    World world;
    world.RegisterComponent<AttributeSet>();
    AttributeRegistry& reg = world.AddResource<AttributeRegistry>();
    const AttributeId hp = reg.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);

    EntityId e = world.CreateEntity();
    world.AddComponent<AttributeSet>(e);
    ASSERT_TRUE(world.TryGet<AttributeSet>(e)->Add(hp, 250.0f)); // above max

    ResolveAttributes(world);
    EXPECT_FLOAT_EQ(world.TryGet<AttributeSet>(e)->GetCurrent(hp), 100.0f);

    ASSERT_TRUE(world.TryGet<AttributeSet>(e)->SetBase(hp, -5.0f)); // below min
    ResolveAttributes(world);
    EXPECT_FLOAT_EQ(world.TryGet<AttributeSet>(e)->GetCurrent(hp), 0.0f);
}
