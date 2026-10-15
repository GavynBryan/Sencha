#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

// A stable key is a short identifier, and no two elements of one array share one.
namespace
{
DataFieldSchema KeyedElements()
{
    DataFieldSchema element = MakeDataField(DataFieldKind::Record, "element", "Element");
    element.Children.push_back(MakeDataField(DataFieldKind::StableKey, "key", "Key"));
    DataFieldSchema elements = MakeDataField(DataFieldKind::Array, "elements", "Elements");
    elements.Children.push_back(std::move(element));
    return elements;
}

std::vector<DataValidationError> Validate(std::string_view json)
{
    DataSchema schema;
    schema.TypeName = "test.keys";
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children.push_back(KeyedElements());
    std::vector<DataValidationError> errors;
    (void)ValidateDataAgainstSchema(*JsonParse(json), schema, errors);
    return errors;
}
}

TEST(StableKeyValidation, AcceptsShortIdentifiers)
{
    EXPECT_TRUE(IsValidStableKey("p_charge"));
    EXPECT_TRUE(IsValidStableKey("4b1e0c7d9a2f6e83"));
    EXPECT_FALSE(IsValidStableKey(""));
    EXPECT_FALSE(IsValidStableKey("has space"));
    EXPECT_FALSE(IsValidStableKey(std::string(65, 'a')));
}

TEST(StableKeyValidation, TwoElementsCannotShareAKey)
{
    EXPECT_TRUE(Validate(R"({ "elements": [ { "key": "a" }, { "key": "b" } ] })").empty());
    const std::vector<DataValidationError> errors = Validate(R"({ "elements": [ { "key": "a" }, { "key": "a" } ] })");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors.front().Path, "$.elements[1].key");
}
