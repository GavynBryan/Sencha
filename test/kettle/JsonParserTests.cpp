#include <gtest/gtest.h>
#include <json/JsonValue.h>
#include <json/JsonParser.h>

// --- Primitives --------------------------------------------------------------

TEST(JsonParser, ParseNull)
{
	auto result = JsonParse("null");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsNull());
}

TEST(JsonParser, ParseTrue)
{
	auto result = JsonParse("true");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsBool());
	EXPECT_TRUE(result->AsBool());
}

TEST(JsonParser, ParseFalse)
{
	auto result = JsonParse("false");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsBool());
	EXPECT_FALSE(result->AsBool());
}

TEST(JsonParser, ParseInteger)
{
	auto result = JsonParse("42");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsNumber());
	EXPECT_DOUBLE_EQ(result->AsNumber(), 42.0);
}

TEST(JsonParser, ParseNegativeNumber)
{
	auto result = JsonParse("-3.14");
	ASSERT_TRUE(result.has_value());
	EXPECT_NEAR(result->AsNumber(), -3.14, 0.001);
}

TEST(JsonParser, ParseExponentNumber)
{
	auto result = JsonParse("1.5e2");
	ASSERT_TRUE(result.has_value());
	EXPECT_DOUBLE_EQ(result->AsNumber(), 150.0);
}

TEST(JsonParser, ParseString)
{
	auto result = JsonParse("\"hello world\"");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsString());
	EXPECT_EQ(result->AsString(), "hello world");
}

TEST(JsonParser, ParseStringWithEscapes)
{
	auto result = JsonParse(R"("line1\nline2\ttab\\slash\"quote")");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->AsString(), "line1\nline2\ttab\\slash\"quote");
}

TEST(JsonParser, ParseStringWithUnicodeEscape)
{
	auto result = JsonParse(R"("\u0041")");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->AsString(), "A");
}

// --- Arrays ------------------------------------------------------------------

TEST(JsonParser, ParseEmptyArray)
{
	auto result = JsonParse("[]");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsArray());
	EXPECT_EQ(result->Size(), 0u);
}

TEST(JsonParser, ParseArray)
{
	auto result = JsonParse("[1, 2, 3]");
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->Size(), 3u);
	EXPECT_DOUBLE_EQ(result->AsArray()[0].AsNumber(), 1.0);
	EXPECT_DOUBLE_EQ(result->AsArray()[1].AsNumber(), 2.0);
	EXPECT_DOUBLE_EQ(result->AsArray()[2].AsNumber(), 3.0);
}

TEST(JsonParser, ParseMixedArray)
{
	auto result = JsonParse(R"([1, "two", true, null])");
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->Size(), 4u);
	EXPECT_TRUE(result->AsArray()[0].IsNumber());
	EXPECT_TRUE(result->AsArray()[1].IsString());
	EXPECT_TRUE(result->AsArray()[2].IsBool());
	EXPECT_TRUE(result->AsArray()[3].IsNull());
}

// --- Objects -----------------------------------------------------------------

TEST(JsonParser, ParseEmptyObject)
{
	auto result = JsonParse("{}");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->IsObject());
	EXPECT_EQ(result->Size(), 0u);
}

TEST(JsonParser, ParseObject)
{
	auto result = JsonParse(R"({"name": "Jump", "id": 1})");
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->IsObject());

	const auto* name = result->Find("name");
	ASSERT_NE(name, nullptr);
	EXPECT_EQ(name->AsString(), "Jump");

	const auto* id = result->Find("id");
	ASSERT_NE(id, nullptr);
	EXPECT_DOUBLE_EQ(id->AsNumber(), 1.0);
}

TEST(JsonParser, FindReturnsNullForMissingKey)
{
	auto result = JsonParse(R"({"a": 1})");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->Find("b"), nullptr);
}

// --- Nested ------------------------------------------------------------------

TEST(JsonParser, ParseNestedStructure)
{
	auto result = JsonParse(R"({
		"actions": [
			{"name": "Jump"},
			{"name": "Pause"}
		],
		"count": 2
	})");
	ASSERT_TRUE(result.has_value());

	const auto* actions = result->Find("actions");
	ASSERT_NE(actions, nullptr);
	ASSERT_TRUE(actions->IsArray());
	ASSERT_EQ(actions->Size(), 2u);

	const auto* name0 = actions->AsArray()[0].Find("name");
	ASSERT_NE(name0, nullptr);
	EXPECT_EQ(name0->AsString(), "Jump");

	const auto* count = result->Find("count");
	ASSERT_NE(count, nullptr);
	EXPECT_DOUBLE_EQ(count->AsNumber(), 2.0);
}

// --- Whitespace handling -----------------------------------------------------

TEST(JsonParser, HandlesLeadingAndTrailingWhitespace)
{
	auto result = JsonParse("  \n\t  42  \n  ");
	ASSERT_TRUE(result.has_value());
	EXPECT_DOUBLE_EQ(result->AsNumber(), 42.0);
}

// --- Error cases -------------------------------------------------------------

TEST(JsonParser, RejectsEmptyInput)
{
	JsonParseError error;
	auto result = JsonParse("", &error);
	EXPECT_FALSE(result.has_value());
	EXPECT_FALSE(error.Message.empty());
}

TEST(JsonParser, RejectsTrailingContent)
{
	JsonParseError error;
	auto result = JsonParse("42 extra", &error);
	EXPECT_FALSE(result.has_value());
}

TEST(JsonParser, RejectsUnterminatedString)
{
	JsonParseError error;
	auto result = JsonParse("\"unterminated", &error);
	EXPECT_FALSE(result.has_value());
}

TEST(JsonParser, RejectsInvalidToken)
{
	JsonParseError error;
	auto result = JsonParse("undefined", &error);
	EXPECT_FALSE(result.has_value());
}

TEST(JsonParser, ErrorReportsPosition)
{
	JsonParseError error;
	auto result = JsonParse("[1, 2, ]", &error);
	EXPECT_FALSE(result.has_value());
	EXPECT_GT(error.Position, 0u);
}
