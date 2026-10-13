#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "tools/ToolActions.h"
#include "tools/ToolRegistry.h"

#include <gtest/gtest.h>

// Every registered tool is an action that puts it in hand, and the operation
// facet names the tool in hand among the others.
TEST_F(WorkspaceTest, EveryToolIsAnActionAndTheOneInHandIsTheOperation)
{
    ActionCatalog catalog;
    ActionContext context;
    ActionContextStack stack;
    const ActionContextStack::Layer layer = stack.Push(context, ActionRank::Workspace);
    ToolRegistry& tools = *Workspace.Interaction.Tools;
    OfferToolActions(catalog, context, [&tools] { return &tools; });

    for (const std::unique_ptr<ITool>& tool : tools.GetTools())
        EXPECT_NE(catalog.Find("tool." + std::string(tool->GetId())), nullptr) << tool->GetId();
    EXPECT_NE(catalog.Find("tool.wheel"), nullptr);

    ASSERT_TRUE(stack.Invoke("tool.place"));
    ASSERT_NE(tools.GetActiveTool(), nullptr);
    EXPECT_EQ(tools.GetActiveTool()->GetId(), "place");
    const WorkspaceOperationFacet operation = ToolOperation(&tools, nullptr);
    EXPECT_EQ(operation.Name, tools.GetActiveTool()->GetDisplayName());
    EXPECT_EQ(std::ranges::count_if(operation.Choices, &OperationChoice::Current), 1);
}
