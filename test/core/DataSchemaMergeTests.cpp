#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    DataFieldSchema Member(DataFieldKind kind, std::string key, bool required = false)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), "");
        field.Required = required;
        return field;
    }

    DataFieldSchema Record(std::string key, std::vector<DataFieldSchema> members)
    {
        DataFieldSchema record = Member(DataFieldKind::Record, std::move(key));
        record.Children = std::move(members);
        return record;
    }

    DataFieldSchema List(std::string key, DataFieldSchema element)
    {
        DataFieldSchema list = Member(DataFieldKind::Array, std::move(key));
        list.Children.push_back(std::move(element));
        return list;
    }

    DataSchema Definition()
    {
        DataSchema schema;
        schema.TypeName = "test.definition";
        schema.Root = Record("", { Member(DataFieldKind::Float, "req", true),
                                   List("mods", Record("", { Member(DataFieldKind::StableKey, "key", true),
                                                             Member(DataFieldKind::Float, "magnitude", true) })) });
        return schema;
    }
}

TEST(DataSchemaMerge, AListsMergeKeyIsItsOneStableKeyOrTheMemberItNames)
{
    const DataFieldSchema one = List("l", Record("", { Member(DataFieldKind::StableKey, "key"), Member(DataFieldKind::String, "v") }));
    ASSERT_NE(ArrayMergeKey(one), nullptr);
    EXPECT_EQ(ArrayMergeKey(one)->Key, "key");

    const DataFieldSchema two = List("l", Record("", { Member(DataFieldKind::StableKey, "a"), Member(DataFieldKind::StableKey, "b") }));
    EXPECT_EQ(ArrayMergeKey(two), nullptr) << "two keys name no one key";

    DataFieldSchema named = List("l", Record("", { Member(DataFieldKind::String, "name") }));
    EXPECT_EQ(ArrayMergeKey(named), nullptr);
    named.MergeKey = "name";
    ASSERT_NE(ArrayMergeKey(named), nullptr);
    EXPECT_EQ(ArrayMergeKey(named)->Key, "name");

    EXPECT_EQ(ArrayMergeKey(List("l", Member(DataFieldKind::String, ""))), nullptr);
}

TEST(DataSchemaMerge, PartialValidationChecksKindsAndKeysButNotRequiredMembers)
{
    const DataSchema schema = Definition();
    const JsonValue partial = *JsonParse(R"({ "mods": [ { "key": "m1" } ] })");

    std::vector<DataValidationError> complete;
    EXPECT_FALSE(ValidateDataAgainstSchema(partial, schema, complete));
    EXPECT_EQ(complete.size(), 2u) << "the root's req and the element's magnitude";

    std::vector<DataValidationError> errors;
    EXPECT_TRUE(ValidateDataAgainstSchema(partial, schema, errors, DataValidationMode::Partial));

    const JsonValue wrong = *JsonParse(R"({ "req": "fast", "mods": [ { "key": "bad key" }, { "key": "m1" }, { "key": "m1" } ] })");
    errors.clear();
    EXPECT_FALSE(ValidateDataAgainstSchema(wrong, schema, errors, DataValidationMode::Partial));
    ASSERT_EQ(errors.size(), 3u) << "kinds, key format and key uniqueness still hold";
    EXPECT_EQ(errors[0].Path, "$.req");
}
