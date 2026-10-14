#include "WorkspaceFixture.h"

#include "authoring/BrushModifierRows.h"
#include "authoring/ComponentPropertySource.h"
#include "brush/BrushModifier.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <variant>

// Modifiers are an open set: every kind the stack knows is offered from one
// menu that says what each does, and each reads and edits through its own
// presentation, so a new kind is never a new branch here.
namespace
{
class BrushModifierRowsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Brush = AddBrush({ 0, 0, 0 });
        SelectEntity(Brush);
    }

    [[nodiscard]] const PropertyNode* Find(std::string_view key) const
    {
        static std::vector<PropertyNode> nodes;
        nodes = Components.Nodes();
        const auto search = [&](const auto& self, const std::vector<PropertyNode>& level) -> const PropertyNode* {
            for (const PropertyNode& node : level)
            {
                if (node.Key == key)
                    return &node;
                if (const PropertyNode* found = self(self, node.Children))
                    return found;
            }
            return nullptr;
        };
        return search(search, nodes);
    }

    EntityId Brush{};
    ComponentPropertySource Components{ Workspace.World, Workspace.Selection, Commands,
                                        Workspace.Affordances->Registry() };
};
} // namespace

TEST(BrushModifierRows, EveryKindIsNamedAndSaysWhatItDoes)
{
    for (const BrushModifierKindInfo& kind : BrushModifierKinds())
    {
        const ModifierKindText text =
            std::visit([](const auto& params) { return ModifierText(params); }, kind.MakeDefault().Params);
        EXPECT_FALSE(text.Label.empty()) << kind.JsonKey;
        EXPECT_FALSE(text.Description.empty()) << kind.JsonKey;
    }
}

TEST_F(BrushModifierRowsTest, TheMenuOffersEveryKindWithWhatItDoes)
{
    const PropertyNode* stack = Find("brush:stack");
    ASSERT_NE(stack, nullptr);
    const auto add = std::ranges::find(stack->Descriptor.Verbs, "add", &PropertyVerb::Id);
    ASSERT_NE(add, stack->Descriptor.Verbs.end());
    EXPECT_EQ(add->Choices.size(), BrushModifierKinds().size());
    EXPECT_EQ(add->ChoiceNotes.size(), add->Choices.size());
}

TEST_F(BrushModifierRowsTest, AChosenKindIsAddedAsOneStepAndReadsAsItself)
{
    ASSERT_EQ(Components.Act("brush:stack", "add", "Array"), PropertyEditResult::Applied);
    const PropertyNode* row = Find("brush:m0");
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->Descriptor.Label.starts_with("Array")) << row->Descriptor.Label;
    const auto remove = std::ranges::find(row->Descriptor.Verbs, "remove", &PropertyVerb::Id);
    ASSERT_NE(remove, row->Descriptor.Verbs.end());
    EXPECT_TRUE(remove->Removes);

    ASSERT_TRUE(Components.BeginEdit("brush:m0.count"));
    EXPECT_EQ(Components.CommitEdit("brush:m0.count", "4"), PropertyEditResult::Applied);
    EXPECT_NE(Find("brush:m0")->Descriptor.Label.find("\xC3\x97" "4"), std::string::npos);

    Commands.Undo();
    Commands.Undo();
    EXPECT_EQ(Find("brush:m0"), nullptr) << "undoing both leaves the stack as it was";
}
