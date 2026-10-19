#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/JsonArchive.h>
#include <gtest/gtest.h>
#include <sstream>

namespace
{
JsonValue Example()
{
    return *JsonParse(
        R"({"unknown.command":{"target":{"input":"activator"},"args":[null,true,17.25,"Enum",[1,2,3],{}]},"tail":"preserved"})");
}
} // namespace

TEST(ArchiveValue, JsonPreservesNestedUnknownValuesAndMemberOrder)
{
    JsonWriteArchive out;
    out.BeginObject({});
    out.Field("before", std::uint32_t{7});
    out.Value("program", Example());
    out.Field("after", false);
    out.End();
    ASSERT_TRUE(out.Ok());
    const auto document = out.TakeValue();
    JsonReadArchive in(document);
    in.BeginObject({});
    std::uint32_t before = 0;
    bool after = true;
    JsonValue value;
    in.Field("before", before);
    in.Value("program", value);
    in.Field("after", after);
    in.End();
    ASSERT_TRUE(in.Ok());
    EXPECT_EQ(before, 7u);
    EXPECT_FALSE(after);
    EXPECT_EQ(JsonStringify(value), JsonStringify(Example()));
}

TEST(ArchiveValue, BinaryPreservesValuesWithoutChangingAdjacentScalarFields)
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    BinaryWriter writer(stream);
    BinaryWriteArchive out(writer);
    out.Field("before", std::uint32_t{7});
    out.Value("program", Example());
    out.Field("after", false);
    ASSERT_TRUE(out.Ok());
    stream.seekg(0);
    BinaryReader reader(stream);
    BinaryReadArchive in(reader);
    std::uint32_t before = 0;
    bool after = true;
    JsonValue value;
    in.Field("before", before);
    in.Value("program", value);
    in.Field("after", after);
    ASSERT_TRUE(in.Ok());
    EXPECT_EQ(before, 7u);
    EXPECT_FALSE(after);
    EXPECT_EQ(JsonStringify(value), JsonStringify(Example()));
}

TEST(ArchiveValue, MissingJsonValueLeavesTheDestinationUnchanged)
{
    const JsonValue empty(JsonValue::Object{});
    JsonReadArchive in(empty);
    in.BeginObject({});
    JsonValue value = Example();
    in.Value("missing", value);
    EXPECT_FALSE(in.Ok());
    EXPECT_EQ(JsonStringify(value), JsonStringify(Example()));
}

TEST(ArchiveValue, MalformedBinaryJsonLeavesTheDestinationUnchanged)
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    BinaryWriter writer(stream);
    BinaryWriteArchive out(writer);
    out.Field("program", std::string_view("{truncated"));
    stream.seekg(0);
    BinaryReader reader(stream);
    BinaryReadArchive in(reader);
    JsonValue value = Example();
    in.Value("program", value);
    EXPECT_FALSE(in.Ok());
    EXPECT_EQ(JsonStringify(value), JsonStringify(Example()));
}
