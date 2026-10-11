// One check of a JSON value against a declared field, shared by data assets,
// verb bindings and editor operations: what an absent member means, what the
// rules switch, and that the value it hands back is the one it checked.

#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace
{
    JsonValue Parse(std::string_view text)
    {
        return JsonParse(text).value_or(JsonValue{});
    }

    // { radius: Float = 2, label: String (required), count: Int (optional),
    //   owner: Entity (optional), tint: Optional<Vector3> }
    DataFieldSchema Record()
    {
        DataFieldSchema root = MakeDataField(DataFieldKind::Record, "", "");
        DataFieldSchema radius = MakeDataField(DataFieldKind::Float, "radius", "Radius");
        radius.Default = 2.0;
        radius.Numeric.Minimum = 0.0;
        DataFieldSchema label = MakeDataField(DataFieldKind::String, "label", "Label");
        DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
        count.Required = false;
        DataFieldSchema owner = MakeDataField(DataFieldKind::Entity, "owner", "Owner");
        owner.Required = false;
        DataFieldSchema tint = MakeDataField(DataFieldKind::Optional, "tint", "Tint");
        tint.Required = false;
        tint.Children.push_back(MakeDataField(DataFieldKind::Vector, "", ""));
        root.Children = { radius, label, count, owner, tint };
        return root;
    }

    DataValueRules Filling()
    {
        DataValueRules rules;
        rules.FillDefaults = true;
        return rules;
    }
}

TEST(DataValueNormalize, AbsentMembersTakeTheirDefaultOrNull)
{
    std::vector<DataValidationError> errors;
    JsonValue out;
    const JsonValue value = Parse(R"({"label":"hall"})");
    ASSERT_TRUE(NormalizeDataValue(&value,
                                   Record(), "$", Filling(), &out, errors));
    EXPECT_TRUE(errors.empty());
    EXPECT_DOUBLE_EQ(out.Find("radius")->AsNumber(), 2.0);
    EXPECT_EQ(out.Find("label")->AsString(), "hall");
    EXPECT_TRUE(out.Find("count")->IsNull());
    EXPECT_TRUE(out.Find("tint")->IsNull());
}

TEST(DataValueNormalize, ARequiredMemberWithoutADefaultIsReportedByPath)
{
    std::vector<DataValidationError> errors;
    const JsonValue value = Parse(R"({"radius": 1})");
    EXPECT_FALSE(NormalizeDataValue(&value, Record(), "$", Filling(), nullptr, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors.front().Path, "$.label");
}

TEST(DataValueNormalize, WithoutFillingAnAbsentOptionalMemberIsSimplyAbsent)
{
    std::vector<DataValidationError> errors;
    JsonValue out;
    const JsonValue value = Parse(R"({"radius": 1, "label": "hall"})");
    ASSERT_TRUE(NormalizeDataValue(&value, Record(), "$", DataValueRules{}, &out, errors));
    EXPECT_EQ(out.Find("count"), nullptr);
    EXPECT_DOUBLE_EQ(out.Find("radius")->AsNumber(), 1.0);
}

TEST(DataValueNormalize, UnknownMembersAreRefusedUnlessTheRulesAllowThem)
{
    const JsonValue value = Parse(R"({"label": "hall", "colour": "red"})");
    std::vector<DataValidationError> errors;
    EXPECT_FALSE(NormalizeDataValue(&value, Record(), "$", Filling(), nullptr, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors.front().Path, "$.colour");

    DataValueRules lenient = Filling();
    lenient.AllowUnknownFields = true;
    errors.clear();
    EXPECT_TRUE(NormalizeDataValue(&value, Record(), "$", lenient, nullptr, errors));
}

TEST(DataValueNormalize, ValuesAreTypedAndRanged)
{
    struct Case
    {
        const char* Text;
        const char* Path;
    };
    const Case cases[] = {
        { R"({"label": 3})", "$.label" },
        { R"({"label": "a", "radius": -1})", "$.radius" },
        { R"({"label": "a", "count": 1.5})", "$.count" },
        // Past 2^53 a double no longer holds every integer, so the count is refused.
        { R"({"label": "a", "count": 18014398509481984})", "$.count" },
        { R"({"label": "a", "tint": [1, 2]})", "$.tint" },
        { R"({"label": "a", "owner": "not-an-id"})", "$.owner" },
    };
    for (const Case& c : cases)
    {
        std::vector<DataValidationError> errors;
        const JsonValue value = Parse(c.Text);
        EXPECT_FALSE(NormalizeDataValue(&value, Record(), "$", Filling(), nullptr, errors)) << c.Text;
        ASSERT_FALSE(errors.empty()) << c.Text;
        EXPECT_EQ(errors.front().Path, c.Path) << c.Text;
    }
}

TEST(DataValueNormalize, AnExplicitNullOnAnOptionalIsKept)
{
    std::vector<DataValidationError> errors;
    JsonValue out;
    const JsonValue value = Parse(R"({"label": "a", "tint": null})");
    ASSERT_TRUE(NormalizeDataValue(&value, Record(), "$", Filling(), &out, errors));
    EXPECT_TRUE(out.Find("tint")->IsNull());
}

TEST(DataValueNormalize, ReferenceFieldsFollowTheCallersRule)
{
    const JsonValue value = Parse(R"({"label": "a", "owner": "0123456789abcdef"})");
    std::vector<DataValidationError> errors;
    EXPECT_TRUE(NormalizeDataValue(&value, Record(), "$", Filling(), nullptr, errors));

    DataValueRules refusing = Filling();
    refusing.CheckReference = [](const JsonValue&, const DataFieldSchema&) {
        return std::string("written as a reference source");
    };
    errors.clear();
    EXPECT_FALSE(NormalizeDataValue(&value, Record(), "$", refusing, nullptr, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors.front().Message, "written as a reference source");
}

TEST(DataValueNormalize, ValidationIsNormalizationWithoutDefaults)
{
    DataSchema schema;
    schema.Root = Record();
    for (std::string_view text : { R"({"label": "a"})", R"({"radius": 1})", R"({"label": 2})" })
    {
        const JsonValue value = Parse(text);
        std::vector<DataValidationError> viaValidate;
        std::vector<DataValidationError> viaNormalize;
        const bool validated = ValidateDataAgainstSchema(value, schema, viaValidate);
        const bool normalized =
            NormalizeDataValue(&value, schema.Root, "$", DataValueRules{}, nullptr, viaNormalize);
        EXPECT_EQ(validated, normalized) << text;
        EXPECT_EQ(viaValidate.size(), viaNormalize.size()) << text;
    }
}
