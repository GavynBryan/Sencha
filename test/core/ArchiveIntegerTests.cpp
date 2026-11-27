#include <gtest/gtest.h>

#include <core/json/JsonValue.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
struct SignedInner
{
    std::int32_t Offset = 0;
    std::int16_t Small = 0;

    bool operator==(const SignedInner&) const = default;
};

struct SignedOuter
{
    std::int32_t Priority = 0;
    SignedInner Inner;
    std::uint16_t Narrow = 0;
    std::uint32_t Wide = 0;

    bool operator==(const SignedOuter&) const = default;
};
}

template <>
struct TypeSchema<SignedInner>
{
    static constexpr std::string_view Name = "SignedInner";

    static auto Fields()
    {
        return std::tuple{
            MakeField("offset", &SignedInner::Offset),
            MakeField("small", &SignedInner::Small),
        };
    }
};

template <>
struct TypeSchema<SignedOuter>
{
    static constexpr std::string_view Name = "SignedOuter";

    static auto Fields()
    {
        return std::tuple{
            MakeField("priority", &SignedOuter::Priority),
            MakeField("inner", &SignedOuter::Inner),
            MakeField("narrow", &SignedOuter::Narrow),
            MakeField("wide", &SignedOuter::Wide),
        };
    }
};

namespace
{
std::stringstream MakeBinaryStream()
{
    return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
}

JsonValue OuterJson(JsonValue priority, JsonValue narrow, JsonValue offset = JsonValue(0), JsonValue small = JsonValue(0),
                    JsonValue wide = JsonValue(0))
{
    return JsonValue(JsonValue::Object{
        { "priority", std::move(priority) },
        { "inner", JsonValue(JsonValue::Object{ { "offset", std::move(offset) }, { "small", std::move(small) } }) },
        { "narrow", std::move(narrow) },
        { "wide", std::move(wide) },
    });
}

bool ReadsFromBinary(std::int32_t priority, std::int32_t offset, std::int32_t small, std::uint32_t narrow)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    EXPECT_TRUE(writer.Write(priority) && writer.Write(offset) && writer.Write(small) && writer.Write(narrow)
                && writer.Write(std::uint32_t{ 0 }));
    stream.seekg(0);
    BinaryReader reader(stream);
    SignedOuter read{};
    return Deserialize(reader, read);
}
}

TEST(ArchiveIntegers, ANegativeMemberIsWrittenAsANegativeNumberAndReadBack)
{
    const SignedOuter written{ .Priority = -1, .Inner = { .Offset = -1, .Small = -300 }, .Narrow = 7, .Wide = 4000000000u };
    const JsonValue json = ToJson(written);

    ASSERT_NE(json.Find("priority"), nullptr);
    EXPECT_EQ(json.Find("priority")->AsNumber(), -1.0);
    const JsonValue* inner = json.Find("inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->Find("offset")->AsNumber(), -1.0) << "a nested member goes through the same archive value";
    EXPECT_EQ(inner->Find("small")->AsNumber(), -300.0);
    EXPECT_EQ(json.Find("wide")->AsNumber(), 4000000000.0);

    SignedOuter read{};
    ASSERT_TRUE(FromJson(json, read));
    EXPECT_EQ(read, written);
}

TEST(ArchiveIntegers, ANegativeMemberRoundTripsThroughTheBinaryArchiveInFourBytes)
{
    const SignedOuter written{ .Priority = -1, .Inner = { .Offset = -1, .Small = -2 }, .Narrow = 65535, .Wide = 9 };
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(Serialize(writer, written));

    const std::string bytes = stream.str();
    ASSERT_EQ(bytes.size(), 20u) << "every integer member keeps its four bytes";
    EXPECT_EQ(bytes.substr(0, 4), std::string(4, '\xFF'));
    EXPECT_EQ(bytes.substr(4, 4), std::string(4, '\xFF'));

    stream.seekg(0);
    BinaryReader reader(stream);
    SignedOuter read{};
    ASSERT_TRUE(Deserialize(reader, read));
    EXPECT_EQ(read, written);
}

TEST(ArchiveIntegers, ANumberThatDoesNotFitItsMemberIsRefused)
{
    SignedOuter read{};
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(70000)), read)) << "70000 is not a uint16";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(-1)), read)) << "-1 is not a uint16";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(0), JsonValue(0), JsonValue(40000)), read))
        << "40000 is not an int16";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(4294967295.0), JsonValue(0)), read)) << "an int32 holds no 4294967295";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(-2147483649.0), JsonValue(0)), read));
    EXPECT_TRUE(FromJson(OuterJson(JsonValue(-2147483648.0), JsonValue(65535), JsonValue(2147483647.0),
                                   JsonValue(-32768), JsonValue(4294967295.0)),
                         read));
    EXPECT_EQ(read.Priority, -2147483647 - 1);
    EXPECT_EQ(read.Narrow, 65535u);
    EXPECT_EQ(read.Inner.Small, -32768);
    EXPECT_EQ(read.Wide, 4294967295u);

    EXPECT_FALSE(ReadsFromBinary(0, 0, 0, 70000)) << "the binary archive checks the member's width too";
    EXPECT_FALSE(ReadsFromBinary(0, 0, 40000, 0));
    EXPECT_TRUE(ReadsFromBinary(-1, -1, -32768, 65535));
}

TEST(ArchiveIntegers, AFractionIsRefusedIntoAnInteger)
{
    SignedOuter read{};
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(1.5), JsonValue(0)), read)) << "a signed member";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(1.5)), read)) << "an unsigned member";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(0), JsonValue(-0.5)), read)) << "a nested member";
    EXPECT_FALSE(FromJson(OuterJson(JsonValue(0), JsonValue(0), JsonValue(0), JsonValue(0), JsonValue(2.25)), read))
        << "a uint32 member";
    EXPECT_TRUE(FromJson(OuterJson(JsonValue(3.0), JsonValue(0)), read)) << "a whole number written with a point";
    EXPECT_EQ(read.Priority, 3);
}
