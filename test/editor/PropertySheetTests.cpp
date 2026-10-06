#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "properties/SettingProperties.h"
#include "presenters/PropertySheet.h"

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <string>
#include <vector>

// Property nodes become rows whose control follows what the property is, and a
// row's edit goes back to the source as one begin-commit step by key.
namespace
{
class Settings final : public IPropertySource
{
public:
    std::map<std::string, std::string> Values = { { "shape", "Arch" }, { "segments", "6" }, { "pierce", "false" } };
    std::vector<std::string> Log;

    std::vector<PropertyNode> Nodes() const override
    {
        static constexpr std::array<std::string_view, 2> kShapes = { "Rectangle", "Arch" };
        PropertyNode group;
        group.Key = "carve";
        group.Descriptor.Kind = PropertyKind::Group;
        group.Descriptor.Label = "Carve";
        group.Children = { ChoiceProperty("shape", "Shape", kShapes, Values.at("shape") == "Arch" ? 1 : 0),
                           NumberProperty("segments", "Segments", std::stod(Values.at("segments")), 2, 64, 1),
                           ToggleProperty("pierce", "Pierce", Values.at("pierce") == "true") };
        PropertyNode advanced = NumberProperty("tolerance", "Tolerance", 0.01, 0, 1, 0.001);
        advanced.Descriptor.Advanced = true;
        return { group, advanced };
    }
    bool BeginEdit(std::string_view key) override
    {
        Log.push_back("begin " + std::string(key));
        return true;
    }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override
    {
        Log.push_back("commit " + std::string(key) + "=" + std::string(value));
        Values[std::string(key)] = value;
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view key) override { Log.push_back("cancel " + std::string(key)); }
};
} // namespace

TEST(PropertySheet, WhatAPropertyIsDecidesItsControl)
{
    Settings settings;
    PropertySheet sheet;
    const std::vector<UiRow> rows = sheet.Present(settings);
    ASSERT_EQ(rows.size(), 4u) << "the group, its three settings, and no advanced one";
    EXPECT_EQ(rows[0].Control, UiRowControl::Section);
    EXPECT_FALSE(rows[0].Editable);
    EXPECT_EQ(rows[1].Control, UiRowControl::Choice);
    EXPECT_EQ(rows[1].Choices, (std::vector<std::string>{ "Rectangle", "Arch" }));
    EXPECT_EQ(rows[1].Depth, 1);
    EXPECT_EQ(rows[2].Control, UiRowControl::Number) << "a number is a box, filled as far as it is between its bounds";
    EXPECT_EQ(rows[2].Number, 6.0);
    EXPECT_EQ(rows[2].Min, 2.0);
    EXPECT_EQ(rows[2].Max, 64.0);
    EXPECT_EQ(rows[3].Control, UiRowControl::Toggle);

    EXPECT_EQ(sheet.Present(settings, /*everything*/ true).size(), 5u);
}

TEST(PropertySheet, ARowsEditIsOneStepByKey)
{
    Settings settings;
    PropertySheet sheet;
    (void)sheet.Present(settings);

    EXPECT_EQ(sheet.Set(settings, 2, "12"), PropertyEditResult::Applied);
    EXPECT_EQ(settings.Values["segments"], "12");
    EXPECT_EQ(settings.Log, (std::vector<std::string>{ "begin segments", "commit segments=12" }));

    EXPECT_EQ(sheet.Flip(settings, 3), PropertyEditResult::Applied);
    EXPECT_EQ(settings.Values["pierce"], "true");

    EXPECT_EQ(sheet.Set(settings, 1, "Arch"), PropertyEditResult::Unchanged) << "the value it already shows";
    EXPECT_EQ(sheet.Set(settings, 9, "x"), PropertyEditResult::Gone);
}

TEST(PropertySheet, SectionsPresentUnderHeadingsAndEditTheirOwnSource)
{
    Settings first;
    Settings second;
    const PropertySection sections[] = { { .Heading = "First", .Source = &first }, { .Heading = "Second", .Source = &second } };
    PropertySheet sheet;
    const std::vector<UiRow> rows = sheet.Present(sections);
    ASSERT_EQ(rows.size(), 10u) << "a heading and four rows for each";
    EXPECT_EQ(rows[5].Label, "Second");
    EXPECT_EQ(sheet.Set(sections, 8, "40"), PropertyEditResult::Applied);
    EXPECT_EQ(second.Values["segments"], "40");
    EXPECT_EQ(first.Values["segments"], "6");
    EXPECT_EQ(sheet.Set(sections, 5, "x"), PropertyEditResult::Gone) << "a heading holds nothing";
}

TEST(PropertySheet, AFieldHeldWhileTypedCommitsWhenItLetsGo)
{
    Settings settings;
    PropertySheet sheet;
    (void)sheet.Present(settings);
    sheet.Hold(2);
    EXPECT_TRUE(sheet.Holding());
    EXPECT_EQ(sheet.Release(settings, 2, "9"), PropertyEditResult::Applied);
    EXPECT_FALSE(sheet.Holding());
    EXPECT_EQ(settings.Values["segments"], "9");

    sheet.Hold(2);
    sheet.Drop();
    EXPECT_FALSE(sheet.Holding());
    EXPECT_EQ(settings.Values["segments"], "9") << "dropping commits nothing";
}

TEST(PropertySheet, ControlsReportingAsTheyAreLaidOutChangeNothing)
{
    Settings settings;
    PropertySheet sheet;
    (void)sheet.Present(settings);
    // A control made this frame reports where it started, before its value bound.
    EXPECT_EQ(sheet.SetFromControl(settings, 1, UiValue(std::string("Rectangle"))), PropertyEditResult::Unchanged);
    EXPECT_EQ(settings.Values["shape"], "Arch");

    (void)sheet.Present(settings);
    EXPECT_EQ(sheet.SetFromControl(settings, 1, UiValue(std::string("Rectangle"))), PropertyEditResult::Applied);
    EXPECT_EQ(settings.Values["shape"], "Rectangle");
}

namespace
{
class Names final : public IPropertySource
{
public:
    std::vector<std::string> Offered = { "Speed" };
    std::string Value = "Speed";
    std::vector<std::string> Log;

    std::vector<PropertyNode> Nodes() const override
    {
        PropertyNode fact = TextProperty("fact", "Fact", Value);
        fact.Descriptor.Kind = PropertyKind::Choice;
        fact.Descriptor.Choices = Offered;
        return { fact };
    }
    bool BeginEdit(std::string_view) override { return true; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override
    {
        Log.push_back(std::string(key) + "=" + std::string(value));
        Value = value;
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override {}
};
} // namespace

// A drop-down offered other options under the same key settles on its value
// anew: the option it reports while it does is not a person choosing one.
TEST(PropertySheet, ADropDownWhoseOptionsChangedSettlesBeforeItEdits)
{
    Names names;
    PropertySheet sheet;
    (void)sheet.Present(names);
    (void)sheet.Present(names);
    names.Offered = { "Grounded", "Speed" };
    (void)sheet.Present(names);
    EXPECT_EQ(sheet.SetFromControl(names, 0, UiValue(std::string("Grounded"))), PropertyEditResult::Unchanged);
    EXPECT_TRUE(names.Log.empty());

    (void)sheet.Present(names);
    EXPECT_EQ(sheet.SetFromControl(names, 0, UiValue(std::string("Grounded"))), PropertyEditResult::Applied);
    EXPECT_EQ(names.Value, "Grounded");
}

// What can be done about a property is offered beside it, never as a pile of
// rows after it: its verbs, the cross that takes it away, and the catalog
// actions it names that can run now. A group worth knowing about but not worth
// reading every time starts closed and stays as it was left.
namespace
{
class Body final : public IPropertySource
{
public:
    std::vector<std::string> Log;
    bool Folded = false;

    std::vector<PropertyNode> Nodes() const override
    {
        PropertyNode body{ .Key = "body", .Descriptor = { .Kind = PropertyKind::Group, .Label = "Body" } };
        body.Descriptor.Verbs = { { .Id = "reset", .Label = "Reset" },
                                  { .Id = "remove", .Label = "Remove", .Removes = true } };
        body.Descriptor.Actions = { "body.bake", "body.export", "body.weld", "body.split", "body.locked" };
        body.Descriptor.Folded = Folded;
        body.Children = { NumberProperty("body/mass", "Mass", 2.0, 0.0, -1.0, 0.1) };
        return { body };
    }
    bool BeginEdit(std::string_view) override { return true; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view) override
    {
        Log.push_back(std::string(verb) + " " + std::string(key));
        return PropertyEditResult::Applied;
    }
};

class Actions
{
public:
    Actions()
    {
        for (const char* id : { "body.bake", "body.export", "body.weld", "body.split", "body.locked" })
            (void)Catalog.Define({ .Id = id, .Label = std::string("Do ") + id, .Category = "Body" });
        for (const char* id : { "body.export", "body.weld", "body.split" })
            Context.Claim(id, { .Invoke = [this, id] { Ran.push_back(id); } });
        Context.Claim("body.locked", { .Invoke = [] {}, .Enabled = [] { return false; } });
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
    std::vector<std::string> Ran;
};
} // namespace

TEST(PropertySheet, WhatCanBeDoneAboutAGroupIsBesideItsHeadingAndRemovalIsMarked)
{
    Body body;
    PropertySheet sheet;
    const std::vector<UiRow> rows = sheet.Present(body);
    ASSERT_EQ(rows.size(), 2u) << "the heading and its field; no rows of verbs";
    ASSERT_EQ(rows[0].Verbs.size(), 2u) << "without a catalog, no actions are offered";
    EXPECT_FALSE(rows[0].Verbs[0].Removes);
    EXPECT_TRUE(rows[0].Verbs[1].Removes);
    EXPECT_TRUE(sheet.Removes(0, "remove"));
    EXPECT_EQ(sheet.LabelOf(0), "Body");

    EXPECT_EQ(sheet.Act(body, 0, "remove"), PropertyEditResult::Applied);
    EXPECT_EQ(sheet.Act(body, 0, "explode"), PropertyEditResult::Gone) << "a verb the row does not offer";
    EXPECT_EQ(body.Log, (std::vector<std::string>{ "remove body" }));
}

TEST(PropertySheet, AGroupsActionsAreOfferedOnlyWhileTheyCanRunAndAtMostThree)
{
    Body body;
    Actions actions;
    PropertySheet sheet;
    sheet.OfferActions(actions.Catalog, actions.Stack);
    const std::vector<UiRow> rows = sheet.Present(body);
    std::vector<std::string> offered;
    for (const UiRowVerb& verb : rows[0].Verbs)
        offered.push_back(verb.Key);
    EXPECT_EQ(offered, (std::vector<std::string>{ "body.export", "body.weld", "body.split", "reset", "remove" }))
        << "unclaimed and disabled actions are not offered, and three is all there is room for";
    EXPECT_EQ(rows[0].Verbs[0].Label, "Do body.export");

    EXPECT_EQ(sheet.Act(body, 0, "body.weld"), PropertyEditResult::Applied);
    EXPECT_EQ(actions.Ran, (std::vector<std::string>{ "body.weld" }));
    EXPECT_TRUE(body.Log.empty()) << "an action is the catalog's to run, not the source's";
}

TEST(PropertySheet, AFoldedGroupShowsItsHeadingAloneAndStaysAsItWasLeft)
{
    Body body;
    body.Folded = true;
    PropertySheet sheet;
    std::vector<UiRow> rows = sheet.Present(body);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].Value, "closed");
    EXPECT_FALSE(rows[0].Expanded);

    sheet.Fold(0);
    rows = sheet.Present(body);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].Value, "open");
    EXPECT_EQ(sheet.Present(body).size(), 2u) << "opened by hand, it stays open";

    sheet.Fold(1);
    EXPECT_EQ(sheet.Present(body).size(), 2u) << "a field does not fold";
}

// A vector is its numbers side by side, each named; one standing for several
// shows nothing; and one number set keeps the others -- through its own key
// where the source has one, or the whole vector where it does not.
namespace
{
class Places final : public IPropertySource
{
public:
    bool Keyed = true;
    std::string Offset = "1, 2, 3";
    std::vector<std::string> Log;

    std::vector<PropertyNode> Nodes() const override
    {
        static constexpr double kOffset[] = { 1, 2, 3 };
        PropertyNode offset = VectorProperty("offset", "Offset", kOffset);
        offset.Value = Offset;
        if (Keyed)
            for (std::size_t i = 0; i < 3; ++i)
                offset.Children.push_back({ .Key = "offset[" + std::to_string(i) + "]",
                                            .Descriptor = { .Kind = PropertyKind::Number,
                                                            .Label = std::string(1, static_cast<char>('A' + i)) },
                                            .Value = i == 1 ? "" : std::to_string(i + 1),
                                            .Mixed = i == 1 });
        return { offset };
    }
    bool BeginEdit(std::string_view) override { return true; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override
    {
        Log.push_back(std::string(key) + "=" + std::string(value));
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override {}
};
} // namespace

TEST(PropertySheet, AVectorIsItsNumbersSideBySideEachSetOnItsOwn)
{
    Places places;
    const PropertySection sections[] = { { .Heading = {}, .Source = &places } };
    PropertySheet sheet;
    std::vector<UiRow> rows = sheet.Present(sections);
    ASSERT_EQ(rows.size(), 1u) << "its numbers are not rows of their own";
    EXPECT_EQ(rows[0].Control, UiRowControl::Vector);
    EXPECT_EQ(rows[0].Choices, (std::vector<std::string>{ "A", "B", "C" }));
    EXPECT_EQ(rows[0].Parts, (std::vector<std::string>{ "1", "", "3" })) << "the one standing for several shows nothing";
    EXPECT_EQ(sheet.EditKeyOf(0, 1), "offset[1]");
    EXPECT_EQ(sheet.SetElement(sections, 0, 1, "7"), PropertyEditResult::Applied);

    places.Keyed = false;
    rows = sheet.Present(sections);
    EXPECT_EQ(rows[0].Choices, (std::vector<std::string>{ "X", "Y", "Z" }));
    EXPECT_EQ(rows[0].Parts, (std::vector<std::string>{ "1", "2", "3" }));
    EXPECT_TRUE(sheet.EditKeyOf(0, 1).empty());
    EXPECT_EQ(sheet.SetElement(sections, 0, 2, "9"), PropertyEditResult::Applied);
    EXPECT_EQ(places.Log, (std::vector<std::string>{ "offset[1]=7", "offset=1, 2, 9" }));
}
