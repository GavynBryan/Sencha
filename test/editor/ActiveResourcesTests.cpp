#include "resources/ActiveResources.h"

#include <gtest/gtest.h>

#include <vector>

// What the person is holding: one active resource per kind, a stash beside it,
// and word of a change reaching whoever still listens.

namespace
{
AssetRef Material(const char* path) { return AssetRef{ AssetType::Material, path }; }
AssetRef Scene(const char* path) { return AssetRef{ AssetType::Scene, path }; }
} // namespace

TEST(ActiveResources, EachKindHoldsItsOwn)
{
    ActiveResources held;
    EXPECT_FALSE(held.Active(AssetType::Material).IsValid());
    held.SetActive(Material("materials/brick.smat"));
    held.SetActive(Scene("scenes/crate.sscene"));
    EXPECT_EQ(held.Active(AssetType::Material).Path, "materials/brick.smat");
    EXPECT_EQ(held.Active(AssetType::Scene).Path, "scenes/crate.sscene");
}

TEST(ActiveResources, AnEmptyStashSlotKeepsWhatIsHeldAndAFullOneTradesPlaces)
{
    ActiveResources held;
    held.SetActive(Material("materials/brick.smat"));
    held.UseStashSlot(AssetType::Material, 1);
    EXPECT_EQ(held.Stash(AssetType::Material)[1].Path, "materials/brick.smat");
    EXPECT_EQ(held.Active(AssetType::Material).Path, "materials/brick.smat") << "storing is not trading";

    held.SetActive(Material("materials/steel.smat"));
    held.UseStashSlot(AssetType::Material, 1);
    EXPECT_EQ(held.Active(AssetType::Material).Path, "materials/brick.smat");
    EXPECT_EQ(held.Stash(AssetType::Material)[1].Path, "materials/steel.smat");

    held.ClearStashSlot(AssetType::Material, 1);
    EXPECT_FALSE(held.Stash(AssetType::Material)[1].IsValid());
    held.UseStashSlot(AssetType::Material, ActiveResources::kStashSlots); // out of range: ignored
}

TEST(ActiveResources, AChangeReachesListenersWhileTheyKeepTheirToken)
{
    ActiveResources held;
    std::vector<AssetType> heard;
    auto token = held.Subscribe([&](AssetType type) { heard.push_back(type); });

    held.SetActive(Material("materials/brick.smat"));
    held.SetActive(Material("materials/brick.smat"));
    EXPECT_EQ(heard.size(), 1u) << "holding the same thing again is no change";

    held.SetActive(Material("materials/steel.smat"));
    held.UseStashSlot(AssetType::Material, 0);
    held.UseStashSlot(AssetType::Material, 0);
    EXPECT_EQ(heard.size(), 3u) << "a store is silent; a trade changes what is held";

    token.reset();
    held.SetActive(Material("materials/glass.smat"));
    EXPECT_EQ(heard.size(), 3u) << "a dropped token still heard a change";
}
