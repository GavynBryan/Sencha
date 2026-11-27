#include <gtest/gtest.h>

#include "data/DataPath.h"

#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct PathCase
{
    std::string_view Path;
    bool Parses;
    std::vector<DataPathStep> Steps;
    std::optional<std::string_view> Parent;
    std::optional<std::size_t> LastIndex;
};

DataPathStep Member(std::string key)
{
    return { DataPathStepKind::Member, std::move(key) };
}

DataPathStep At(std::size_t index)
{
    return { DataPathStepKind::Index, {}, index };
}

DataPathStep Keyed(std::string key)
{
    return { DataPathStepKind::Keyed, std::move(key) };
}

bool SameSteps(const std::vector<DataPathStep>& a, const std::vector<DataPathStep>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].Kind != b[i].Kind || a[i].Key != b[i].Key || a[i].Index != b[i].Index)
            return false;
    return true;
}
} // namespace

TEST(DataPath, ReadsEachPathAsItsStepsItsParentAndItsLastIndex)
{
    const std::vector<PathCase> cases{
        { "$.data", true, {}, std::nullopt, std::nullopt },
        { "$.data.speed", true, { Member("speed") }, "$.data", std::nullopt },
        { "$.data.layers[2]", true, { Member("layers"), At(2) }, "$.data.layers", 2 },
        { "$.data.layers[2].rows[0].any[13]",
          true,
          { Member("layers"), At(2), Member("rows"), At(0), Member("any"), At(13) },
          "$.data.layers[2].rows[0].any",
          13 },
        { "$.data.sections[1].loop", true, { Member("sections"), At(1), Member("loop") }, "$.data.sections[1]",
          std::nullopt },
        { "$.data.mods{m_armor}", true, { Member("mods"), Keyed("m_armor") }, "$.data.mods", std::nullopt },
        { "$.data.mods{m_armor}.args{a-1}.value",
          true,
          { Member("mods"), Keyed("m_armor"), Member("args"), Keyed("a-1"), Member("value") },
          "$.data.mods{m_armor}.args{a-1}",
          std::nullopt },
        { "$.other.speed", false, {}, std::nullopt, std::nullopt },
        { "$.data.layers[x]", false, {}, std::nullopt, std::nullopt },
        { "$.data.layers[2", false, {}, std::nullopt, std::nullopt },
        { "$.data.mods{}", false, {}, std::nullopt, std::nullopt },
        { "$.data.mods{m_armor", false, {}, std::nullopt, std::nullopt },
        { "$.dataspeed", false, {}, std::nullopt, std::nullopt },
    };
    for (const PathCase& test : cases)
    {
        SCOPED_TRACE(std::string(test.Path));
        const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(test.Path);
        ASSERT_EQ(steps.has_value(), test.Parses);
        if (steps)
        {
            EXPECT_TRUE(SameSteps(*steps, test.Steps));
        }
        EXPECT_EQ(DataPathParent(test.Path), test.Parent);
        EXPECT_EQ(DataPathLastIndex(test.Path), test.LastIndex);
    }
}

TEST(DataPath, FindsTheValueAPathNamesAndNothingWhereNoneIs)
{
    const std::optional<JsonValue> data =
        JsonParse(R"({"speed": 3, "layers": [{"name": "base"}, {"name": "upper", "rows": [1, 2]}]})");
    ASSERT_TRUE(data.has_value());
    struct FindCase
    {
        std::string_view Path;
        std::optional<std::string> Text;
        std::optional<double> Number;
    };
    const std::vector<FindCase> cases{
        { "$.data.speed", std::nullopt, 3.0 },
        { "$.data.layers[1].name", "upper", std::nullopt },
        { "$.data.layers[1].rows[1]", std::nullopt, 2.0 },
        { "$.data.layers[2].name", std::nullopt, std::nullopt },
        { "$.data.layers[0].rows", std::nullopt, std::nullopt },
        { "$.data.speed.inner", std::nullopt, std::nullopt },
        { "$.other", std::nullopt, std::nullopt },
    };
    for (const FindCase& test : cases)
    {
        SCOPED_TRACE(std::string(test.Path));
        const JsonValue* found = FindDataValue(*data, test.Path);
        if (!test.Text && !test.Number)
        {
            EXPECT_EQ(found, nullptr);
            continue;
        }
        ASSERT_NE(found, nullptr);
        if (test.Text)
        {
            EXPECT_EQ(found->AsString(), *test.Text);
        }
        if (test.Number)
        {
            EXPECT_DOUBLE_EQ(found->AsNumber(), *test.Number);
        }
    }
    EXPECT_EQ(FindDataValue(*data, "$.data"), &*data) << "the root names the data object itself";
}

TEST(DataPath, AKeyedStepFindsTheElementStatingItsKeyOnlyThroughTheSchema)
{
    DataFieldSchema element = MakeDataField(DataFieldKind::Record, "mod", "Modifier");
    element.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"),
                         MakeDataField(DataFieldKind::Float, "amount", "Amount") };
    DataFieldSchema mods = MakeDataField(DataFieldKind::Array, "mods", "Modifiers");
    mods.Children = { element };
    DataFieldSchema root = MakeDataField(DataFieldKind::Record, "root", "Root");
    root.Children = { mods };
    const std::optional<JsonValue> data =
        JsonParse(R"({"mods": [{"key": "a", "amount": 1}, {"key": "b", "amount": 2}]})");
    ASSERT_TRUE(data.has_value());

    const JsonValue* found = FindDataValue(*data, root, "$.data.mods{b}.amount");
    ASSERT_NE(found, nullptr);
    EXPECT_DOUBLE_EQ(found->AsNumber(), 2.0);
    EXPECT_EQ(FindDataValue(*data, root, "$.data.mods{c}"), nullptr);
    EXPECT_EQ(FindDataValue(*data, "$.data.mods{b}"), nullptr) << "without the schema no member is the key";
}
