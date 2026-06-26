// MaterialSetCache: content dedup, ordered retrieval, and member ownership.

#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
    MaterialHandle MakeMaterial(MaterialCache& cache)
    {
        return cache.Create(Material{});
    }
}

TEST(MaterialSetCache, IdenticalListsDedupToOneHandle)
{
    MaterialCache materials;
    MaterialSetCache sets(&materials);

    const std::array list{ MakeMaterial(materials), MakeMaterial(materials) };
    const MaterialSetHandle a = sets.Acquire(list);
    const MaterialSetHandle b = sets.Acquire(list);
    EXPECT_EQ(a, b);
}

TEST(MaterialSetCache, OrderIsPartOfIdentity)
{
    MaterialCache materials;
    MaterialSetCache sets(&materials);

    const MaterialHandle m0 = MakeMaterial(materials);
    const MaterialHandle m1 = MakeMaterial(materials);
    const std::array forward{ m0, m1 };
    const std::array reversed{ m1, m0 };
    EXPECT_NE(sets.Acquire(forward), sets.Acquire(reversed));
}

TEST(MaterialSetCache, GetReturnsMembersInOrder)
{
    MaterialCache materials;
    MaterialSetCache sets(&materials);

    const MaterialHandle m0 = MakeMaterial(materials);
    const MaterialHandle m1 = MakeMaterial(materials);
    const std::array list{ m0, m1 };
    const MaterialSetHandle handle = sets.Acquire(list);

    const std::vector<MaterialHandle>* members = sets.Get(handle);
    ASSERT_NE(members, nullptr);
    ASSERT_EQ(members->size(), 2u);
    EXPECT_EQ((*members)[0], m0);
    EXPECT_EQ((*members)[1], m1);
}

TEST(MaterialSetCache, SetRetainsMembersAndReleasesOnFree)
{
    MaterialCache materials;
    MaterialSetCache sets(&materials);

    const MaterialHandle m = materials.Create(Material{}); // refcount 1
    const std::array list{ m };
    const MaterialSetHandle handle = sets.Acquire(list);   // set retains m -> 2

    materials.Destroy(m); // drop the creation reference -> 1, still alive via the set
    EXPECT_NE(materials.Get(m), nullptr);

    sets.Release(handle); // set freed -> releases m -> 0 -> freed
    EXPECT_EQ(materials.Get(m), nullptr);
}

TEST(MaterialSetCache, RefCountedAcquireKeepsSetUntilLastRelease)
{
    MaterialCache materials;
    MaterialSetCache sets(&materials);

    const std::array list{ MakeMaterial(materials) };
    const MaterialSetHandle a = sets.Acquire(list);
    const MaterialSetHandle b = sets.Acquire(list); // same handle, refcount 2
    ASSERT_EQ(a, b);

    sets.Release(a);
    EXPECT_NE(sets.Get(b), nullptr); // still one reference outstanding
    sets.Release(b);
    EXPECT_EQ(sets.Get(a), nullptr);
}
