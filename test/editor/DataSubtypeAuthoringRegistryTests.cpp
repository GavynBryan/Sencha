#include "data/DataSubtypeAuthoring.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

// A data document's properties ask this registry what its subtype adds and
// never name a subtype themselves. A claim is honoured, a second claim on one
// subtype is refused, and an unclaimed subtype reports nothing so its document
// reads as its schema alone.
namespace
{
class FakeAuthoring final : public IDataSubtypeAuthoring
{
public:
    explicit FakeAuthoring(std::string subtype) : Name(std::move(subtype)) {}
    [[nodiscard]] std::string_view Subtype() const override { return Name; }

private:
    std::string Name;
};

std::unique_ptr<IDataSubtypeAuthoring> Make(std::string subtype)
{
    return std::make_unique<FakeAuthoring>(std::move(subtype));
}
} // namespace

TEST(DataSubtypeAuthoringRegistry, FindsWhatASubtypeAddsBySubtype)
{
    DataSubtypeAuthoringRegistry registry;
    ASSERT_TRUE(registry.Register(Make("movement.profile")));
    ASSERT_TRUE(registry.Register(Make("input.profile")));
    const IDataSubtypeAuthoring* movement = registry.Find("movement.profile");
    ASSERT_NE(movement, nullptr);
    EXPECT_EQ(movement->Subtype(), "movement.profile");
    EXPECT_NE(registry.Find("input.profile"), nullptr);
}

TEST(DataSubtypeAuthoringRegistry, RefusesASecondClaimOnOneSubtype)
{
    DataSubtypeAuthoringRegistry registry;
    ASSERT_TRUE(registry.Register(Make("input.profile")));
    // Two claiming one document would race for its platform events, so the
    // later claim loses rather than shadowing.
    EXPECT_FALSE(registry.Register(Make("input.profile")));
}

TEST(DataSubtypeAuthoringRegistry, ReportsNothingForAnUnclaimedSubtype)
{
    DataSubtypeAuthoringRegistry registry;
    ASSERT_TRUE(registry.Register(Make("input.profile")));
    EXPECT_EQ(registry.Find("animation.rig"), nullptr);
}
