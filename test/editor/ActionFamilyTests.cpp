#include "actions/ActionFamily.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// A family of actions follows its data: a member that went is released, one
// renamed is relabelled, and the same members are not bound again.
namespace
{
std::vector<ActionDefinition> Members(std::vector<std::pair<std::string, std::string>> rows)
{
    std::vector<ActionDefinition> members;
    for (auto& [id, label] : rows)
        members.push_back({ .Id = std::move(id), .Label = std::move(label), .Category = "Zone" });
    return members;
}
} // namespace

TEST(ActionFamily, FollowsItsMembersAndBindsOnlyWhenTheyChange)
{
    ActionCatalog catalog;
    ActionContext context;
    ActionFamily family;
    int binds = 0;
    std::vector<std::string> invoked;
    const auto offer = [&](std::vector<std::pair<std::string, std::string>> rows) {
        const std::vector<ActionDefinition> members = Members(rows);
        family.Offer(catalog, context, members, [&](std::size_t index) {
            ++binds;
            return ActionBinding{ .Invoke = [&invoked, id = members[index].Id] { invoked.push_back(id); } };
        });
    };

    offer({ { "zone.move.1", "Move to Atrium" }, { "zone.move.2", "Move to Vault" } });
    EXPECT_EQ(binds, 2);
    ASSERT_NE(context.Find("zone.move.2"), nullptr);
    offer({ { "zone.move.1", "Move to Atrium" }, { "zone.move.2", "Move to Vault" } });
    EXPECT_EQ(binds, 2) << "the same members are not bound again";

    offer({ { "zone.move.1", "Move to Hall" } });
    EXPECT_EQ(context.Find("zone.move.2"), nullptr) << "a member that went is released";
    ASSERT_NE(catalog.Find("zone.move.1"), nullptr);
    EXPECT_EQ(catalog.Find("zone.move.1")->Label, "Move to Hall");
    EXPECT_EQ(std::vector<std::string>(family.Offered().begin(), family.Offered().end()),
              (std::vector<std::string>{ "zone.move.1" }));
    context.Find("zone.move.1")->Invoke();
    EXPECT_EQ(invoked, (std::vector<std::string>{ "zone.move.1" }));
}
