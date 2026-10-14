#include "WorkspaceFixture.h"

#include "tools/ITool.h"
#include "tools/ToolParameterSource.h"
#include "tools/ToolRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// A tool's settings are properties anything can present: what the tool in hand
// is set to, and setting it by key from text, with no presentation in the tool.
namespace
{
class ToolParameterTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }
    ToolParameterSource Source{ [this] { return Workspace.Interaction.Tools.get(); } };

    [[nodiscard]] std::string ValueOf(std::string_view key)
    {
        for (const PropertyNode& node : Source.Nodes())
            if (node.Key == key)
                return node.Value;
        return "(absent)";
    }
};
} // namespace

TEST_F(ToolParameterTest, TheBrushToolsSettingsFollowItsPrimitive)
{
    ASSERT_TRUE(Tools().Activate("brush"));
    EXPECT_EQ(ValueOf("primitive"), "Box");
    EXPECT_EQ(ValueOf("sides"), "(absent)") << "a box has no sides to set";

    ASSERT_TRUE(Source.BeginEdit("primitive"));
    EXPECT_EQ(Source.CommitEdit("primitive", "Cylinder"), PropertyEditResult::Applied);
    EXPECT_EQ(ValueOf("sides"), "12") << "a cylinder's sides appear once it is one";
    EXPECT_EQ(Tools().GetActiveTool()->GetActiveVariant(Tools().GetContext()), 2)
        << "the setting and the wheel's variant are one state";

    ASSERT_TRUE(Source.BeginEdit("sides"));
    EXPECT_EQ(Source.CommitEdit("sides", "200"), PropertyEditResult::Applied);
    EXPECT_EQ(ValueOf("sides"), "64") << "held to the range the setting declares";
    EXPECT_EQ(Source.CommitEdit("primitive", "Pyramid"), PropertyEditResult::Refused);
}

TEST_F(ToolParameterTest, ACancelledEditPutsBackWhatItBeganFrom)
{
    ASSERT_TRUE(Tools().Activate("facecarve"));
    ASSERT_TRUE(Source.BeginEdit("turn"));
    EXPECT_EQ(Source.PreviewEdit("turn", "45"), PropertyEditResult::Applied);
    EXPECT_EQ(ValueOf("turn"), "45");
    Source.CancelEdit("turn");
    EXPECT_EQ(ValueOf("turn"), "0");
}

TEST_F(ToolParameterTest, EveryToolWithSettingsAnswersForAllOfThem)
{
    // Each setting a tool lists is one it takes back, as the text it showed.
    for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
    {
        ASSERT_TRUE(Tools().Activate(tool->GetId()));
        for (const PropertyNode& node : Source.Nodes())
        {
            ASSERT_TRUE(Source.BeginEdit(node.Key)) << tool->GetId() << "." << node.Key;
            const PropertyEditResult result = Source.CommitEdit(node.Key, node.Value);
            EXPECT_TRUE(result == PropertyEditResult::Unchanged || result == PropertyEditResult::Applied)
                << tool->GetId() << "." << node.Key << " refused its own value " << node.Value;
        }
    }
}
