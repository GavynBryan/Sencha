#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "actions/ContextMenuModel.h"
#include "properties/PropertySource.h"

#include <gtest/gtest.h>

#include <map>

// The wheel for what the pointer is on: it aims first, then offers the
// selection's choices as fans with the one in effect marked, its actions --
// the first few on their own, the rest in a fan -- and the full sheet; the hub
// reads out what it is. A choice is made on the selection as it is then.
namespace
{
class LightSource final : public IPropertySource
{
public:
    [[nodiscard]] std::vector<PropertyNode> Nodes() const override
    {
        PropertyNode type{ .Key = "type", .Descriptor = { .Kind = PropertyKind::Choice, .Label = "Type",
                                                          .Choices = { "Point", "Spot", "Area" } },
                           .Value = Values.at("type") };
        PropertyNode shadows{ .Key = "shadows", .Descriptor = { .Kind = PropertyKind::Toggle, .Label = "Shadows" },
                              .Value = Values.at("shadows") };
        PropertyNode range{ .Key = "range", .Descriptor = { .Kind = PropertyKind::Number, .Label = "Range" },
                            .Value = "8" };
        PropertyNode hidden{ .Key = "bake", .Descriptor = { .Kind = PropertyKind::Choice, .Label = "Bake",
                                                            .Choices = { "a", "b" }, .Advanced = true },
                             .Value = "a" };
        return { type, shadows, range, hidden };
    }
    bool BeginEdit(std::string_view key) override { return Values.contains(std::string(key)); }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override
    {
        Values[std::string(key)] = std::string(value);
        ++Commits;
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override {}

    std::map<std::string, std::string> Values{ { "type", "Spot" }, { "shadows", "true" } };
    int Commits = 0;
};

class ContextMenuModelTest : public ::testing::Test
{
protected:
    ContextMenuModelTest()
    {
        for (const char* id : { "a.one", "a.two", "a.three", "a.four", "a.five", "a.off", "details" })
            (void)Catalog.Define({ .Id = id, .Label = id, .Category = "Test" });
        for (const char* id : { "a.one", "a.two", "a.three", "a.four", "a.five", "details" })
            Context.Claim(id, { .Invoke = [this, id] { Invoked.push_back(id); } });
        Context.Claim("a.off", { .Invoke = [] {}, .Enabled = [] { return false; } });
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    [[nodiscard]] WorkspaceSelectionFacet Selected()
    {
        if (!Selecting)
            return {};
        return { .Title = "lamp_03",
                 .Actions = { "a.one", "a.off", "a.two", "a.three", "a.four", "a.five" },
                 .Sections = {},
                 .Details = { { .Heading = "Light", .Source = &Light } } };
    }

    [[nodiscard]] int IndexOf(const ContextMenuModel& model, std::string_view label) const
    {
        for (int i = 0; i < model.Count(); ++i)
            if (model.Item(i).Label == label)
                return i;
        return -1;
    }

    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
    LightSource Light;
    bool Selecting = true;
    std::vector<std::string> Invoked;
    Vec2d AimedAt{ -1.0f, -1.0f };
    ContextMenuModel Model{ { .Aim = [this](Vec2d pointer) { AimedAt = pointer; },
                              .Selection = [this] { return Selected(); },
                              .Actions = [this] { return &Stack; },
                              .Details = "details" },
                            Catalog };
};
} // namespace

TEST_F(ContextMenuModelTest, ItAimsThenOffersChoicesActionsAndTheSheet)
{
    Model.Opening(Vec2d(320.0f, 200.0f));
    EXPECT_EQ(AimedAt, Vec2d(320.0f, 200.0f));

    const int type = IndexOf(Model, "Type");
    ASSERT_GE(type, 0);
    ASSERT_EQ(Model.Variants(type).size(), 3u);
    EXPECT_EQ(Model.ActiveVariant(type), 1) << "the choice in effect is marked";
    const int shadows = IndexOf(Model, "Shadows");
    ASSERT_GE(shadows, 0);
    EXPECT_EQ(Model.Variants(shadows)[Model.ActiveVariant(shadows)].Label, "On");
    EXPECT_EQ(IndexOf(Model, "Bake"), -1) << "advanced settings stay in the sheet";

    EXPECT_GE(IndexOf(Model, "a.one"), 0);
    EXPECT_EQ(IndexOf(Model, "a.off"), -1) << "what cannot be done now is not offered";
    const int rest = IndexOf(Model, "Actions");
    ASSERT_GE(rest, 0);
    EXPECT_EQ(Model.Variants(rest).size(), 2u) << "past the first three, the actions share a fan";
    EXPECT_EQ(Model.Item(Model.Count() - 1).Label, "More…");

    const std::vector<std::string> summary = Model.Summary();
    ASSERT_GE(summary.size(), 2u);
    EXPECT_EQ(summary[0], "lamp_03");
    EXPECT_EQ(summary[1], "Type  Spot");
}

TEST_F(ContextMenuModelTest, AChoiceIsOneEditAndAnActionIsPerformed)
{
    // One choice per opening, as the gesture makes one.
    Model.Opening({});
    Model.Select(IndexOf(Model, "Type"), 0);
    EXPECT_EQ(Light.Values["type"], "Point");
    EXPECT_EQ(Light.Commits, 1);

    Model.Opening({});
    Model.Select(IndexOf(Model, "Type"), 0);
    EXPECT_EQ(Light.Commits, 1) << "the option already in effect is no change";

    Model.Opening({});
    Model.Select(IndexOf(Model, "Shadows"), -1);
    EXPECT_EQ(Light.Commits, 1) << "a release on a choice's own petal chooses nothing";

    Model.Opening({});
    Model.Select(IndexOf(Model, "a.two"), -1);
    Model.Opening({});
    Model.Select(IndexOf(Model, "Actions"), 1);
    Model.Opening({});
    Model.Select(Model.Count() - 1, -1);
    EXPECT_EQ(Invoked, (std::vector<std::string>{ "a.two", "a.five", "details" }));
}

TEST_F(ContextMenuModelTest, NothingSelectedLeavesTheWheelShut)
{
    Selecting = false;
    Model.Opening({});
    EXPECT_EQ(Model.Count(), 0);
    EXPECT_TRUE(Model.Summary().empty());
}
