#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "actions/ActionWheelModel.h"

#include <gtest/gtest.h>

#include <string>

// A wheel of actions offers, as it opens, those its context can perform now,
// named as the catalog names them, and performs the one chosen.
TEST(ActionWheelModel, OffersWhatCanBeDoneNowAndDoesIt)
{
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define({ .Id = "file.rename", .Label = "Rename", .Category = "Edit" }));
    ASSERT_TRUE(catalog.Define({ .Id = "file.trash", .Label = "Trash", .Category = "Edit" }));
    ASSERT_TRUE(catalog.Define({ .Id = "file.copy", .Label = "Duplicate", .Category = "Edit" }));
    ActionContext context;
    int renamed = 0;
    bool trashable = false;
    context.Claim("file.rename", { .Invoke = [&] { ++renamed; } });
    context.Claim("file.trash", { .Invoke = [] {}, .Enabled = [&] { return trashable; } });
    ActionWheelModel wheel(catalog, context, { "file.rename", "file.trash", "file.copy", "file.unknown" });

    wheel.Opening({});
    ASSERT_EQ(wheel.Count(), 1) << "not enabled, not claimed and not defined are all left off";
    EXPECT_EQ(wheel.Item(0).Label, "Rename");
    wheel.Select(0, -1);
    EXPECT_EQ(renamed, 1);

    trashable = true;
    wheel.Opening({});
    ASSERT_EQ(wheel.Count(), 2);
    EXPECT_EQ(wheel.Item(1).Label, "Trash");
}
