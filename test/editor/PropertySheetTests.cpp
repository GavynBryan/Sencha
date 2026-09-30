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
    EXPECT_EQ(rows[2].Control, UiRowControl::Range);
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
