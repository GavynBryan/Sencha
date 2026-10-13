#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "viewport/ViewArrangementSettings.h"
#include "viewport/ViewportLayout.h"

#include <core/console/ConsoleRegistry.h>

#include <gtest/gtest.h>

// How the views share the content is kept in the console, so it outlives the
// session, and chosen by actions; a projection action brings forward the view
// looking that way.
TEST(ViewArrangementSettings, KeepsTheArrangementAndTheDividerAndChoosesTheView)
{
    ConsoleRegistry console;
    ViewArrangementSettings views(console);
    EXPECT_EQ(views.Kind(), ViewportArrangementKind::Single);
    EXPECT_EQ(views.Divider(), Vec2d(0.5f, 0.5f));

    ViewportLayout layout = ViewportLayout::MakeDefault();
    ActionCatalog catalog;
    ActionContext context;
    ActionContextStack stack;
    const ActionContextStack::Layer layer = stack.Push(context, ActionRank::Workspace);
    views.Offer(catalog, context, layout);

    ASSERT_TRUE(stack.Invoke("view.four"));
    EXPECT_EQ(views.Kind(), ViewportArrangementKind::Quad);
    EXPECT_TRUE(stack.Query("view.four").Checked);
    EXPECT_NE(console.FindCVar("editor.view.arrangement"), nullptr);

    views.SetDivider(Vec2d(0.3f, 0.7f));
    EXPECT_FLOAT_EQ(views.Divider().X, 0.3f);
    EXPECT_FLOAT_EQ(views.Divider().Y, 0.7f);

    ASSERT_TRUE(stack.Invoke("view.top"));
    ASSERT_NE(layout.Active(), nullptr);
    EXPECT_EQ(layout.Active()->Orientation, ViewportOrientation::Top);
    EXPECT_TRUE(stack.Query("view.top").Checked);
}
