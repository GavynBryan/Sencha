// The scene-source JSON5 codec: what it accepts, what it refuses, and the two
// properties everything above it leans on -- semantic round-trip, and
// byte-identical repeated writes so saving twice never churns a diff.

#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace
{
    [[nodiscard]] Json5Value ParseOk(std::string_view text)
    {
        Json5ParseError error;
        std::optional<Json5Value> parsed = Json5Parse(text, &error);
        EXPECT_TRUE(parsed.has_value()) << error.Message;
        return parsed.has_value() ? *parsed : Json5Value{};
    }

    [[nodiscard]] std::string ParseFailure(std::string_view text)
    {
        Json5ParseError error;
        EXPECT_FALSE(Json5Parse(text, &error).has_value())
            << "accepted: " << text;
        return error.Message;
    }

    TEST(Json5, AcceptsTheJson5Surface)
    {
        const Json5Value doc = ParseOk(R"({
            unquoted: 1,
            $dollar_key: 2,
            _underscore: 3,
            'single quoted': [4, 5,],
        })");
        EXPECT_EQ(doc.Members.size(), 4u);
        EXPECT_EQ(doc.Find("$dollar_key")->Number, 2.0);
        EXPECT_EQ(doc.Find("single quoted")->Elements.size(), 2u);
    }

    TEST(Json5, ParsesJson5Numbers)
    {
        const Json5Value doc = ParseOk(
            "{ hex: 0xFF, negHex: -0x10, lead: .5, trail: 2., plus: +3, exp: 1.5e2 }");
        EXPECT_EQ(doc.Find("hex")->Number, 255.0);
        EXPECT_EQ(doc.Find("negHex")->Number, -16.0);
        EXPECT_EQ(doc.Find("lead")->Number, 0.5);
        EXPECT_EQ(doc.Find("trail")->Number, 2.0);
        EXPECT_EQ(doc.Find("plus")->Number, 3.0);
        EXPECT_EQ(doc.Find("exp")->Number, 150.0);
    }

    TEST(Json5, ParsesJson5Strings)
    {
        const Json5Value doc = ParseOk(
            "{ a: 'single \\'quoted\\'', b: \"tab\\tnewline\\n\", "
            "c: '\\x41\\u00e9', d: 'joi\\\nned', e: '\\q' }");
        EXPECT_EQ(doc.Find("a")->Text, "single 'quoted'");
        EXPECT_EQ(doc.Find("b")->Text, "tab\tnewline\n");
        EXPECT_EQ(doc.Find("c")->Text, "A\xC3\xA9");
        EXPECT_EQ(doc.Find("d")->Text, "joined");
        EXPECT_EQ(doc.Find("e")->Text, "q"); // any other escaped char is itself
    }

    TEST(Json5, DecodesSurrogatePairs)
    {
        const Json5Value doc = ParseOk(R"({ emoji: "😀" })");
        EXPECT_EQ(doc.Find("emoji")->Text, "\xF0\x9F\x98\x80");
    }

    TEST(Json5, RejectsWhatSceneDataMustNotHold)
    {
        EXPECT_NE(ParseFailure("{ bad: Infinity }").find("non-finite"), std::string::npos);
        EXPECT_NE(ParseFailure("{ bad: -Infinity }").find("non-finite"), std::string::npos);
        EXPECT_NE(ParseFailure("{ bad: NaN }").find("non-finite"), std::string::npos);
        EXPECT_NE(ParseFailure("{ bad: 1e999 }").find("non-finite"), std::string::npos);
        EXPECT_NE(ParseFailure("{ a: 1, a: 2 }").find("duplicate key"), std::string::npos);
    }

    TEST(Json5, RejectsMalformedTokens)
    {
        (void)ParseFailure("");
        (void)ParseFailure("{ a: 1 } trailing");
        (void)ParseFailure("{ a: 'unterminated }");
        (void)ParseFailure("{ a: /* unterminated }");
        (void)ParseFailure("{ a: 1");
        (void)ParseFailure("[1, 2");
        (void)ParseFailure("{ a: 1.5e }");
        (void)ParseFailure("{ a: 0x }");
        (void)ParseFailure("{ a: '\\01' }");
        (void)ParseFailure("{ a: '\\7' }");
        (void)ParseFailure("{ a: . }");
        (void)ParseFailure("{ 1bad: 2 }");
    }

    TEST(Json5, RejectsADepthBomb)
    {
        std::string bomb;
        for (int i = 0; i < 400; ++i)
            bomb += "[";
        EXPECT_NE(ParseFailure(bomb).find("depth"), std::string::npos);
    }

    TEST(Json5, ReportsLineAndColumn)
    {
        Json5ParseError error;
        EXPECT_FALSE(Json5Parse("{\n  a: 1,\n  a: 2,\n}", &error).has_value());
        EXPECT_EQ(error.Line, 3u);
    }

    TEST(Json5, RoundTripsSemantics)
    {
        const std::string messy = R"(// header
{
  // the door
  door: { locked: true, hp: 12.5, tags: ['metal', "heavy",], },
  'weird key!': [1, .5, 0x10,],
  nested: { a: { b: { c: null } } },
})";
        const Json5Value first = ParseOk(messy);
        const std::string canonical = Json5Write(first);
        const Json5Value second = ParseOk(canonical);
        EXPECT_TRUE(first.SameValueAs(second)) << canonical;
    }

    TEST(Json5, SaveTwiceIsByteIdentical)
    {
        const std::string messy =
            "/* file header */\n"
            "{ b: 2, // trailing thought\n"
            "  a: [1, 2, /* mid */ 3], list: [ { x: 1 }, { y: 2 } ],\n"
            "  s: 'quote', }\n"
            "// footer";
        std::vector<std::string> end;
        Json5ParseError error;
        const std::optional<Json5Value> first = Json5Parse(messy, &error, &end);
        ASSERT_TRUE(first.has_value()) << error.Message;

        const std::string once = Json5Write(*first, end);
        std::vector<std::string> endAgain;
        const std::optional<Json5Value> reparsed = Json5Parse(once, &error, &endAgain);
        ASSERT_TRUE(reparsed.has_value()) << error.Message << "\n" << once;
        const std::string twice = Json5Write(*reparsed, endAgain);
        EXPECT_EQ(once, twice);
    }

    TEST(Json5, CommentsSurviveTheRoundTrip)
    {
        const std::string annotated = R"({
  // the boss door
  door: 1,
  window: 2, // sits between value and comma? no: after the comma
  /* block note */
  wall: 3,
})";
        const Json5Value doc = ParseOk(annotated);
        const std::string written = Json5Write(doc);
        EXPECT_NE(written.find("// the boss door"), std::string::npos);
        EXPECT_NE(written.find("/* block note */"), std::string::npos);
        // Anchoring: the door comment precedes the door member.
        EXPECT_LT(written.find("// the boss door"), written.find("door: 1"));
        EXPECT_LT(written.find("/* block note */"), written.find("wall: 3"));
    }

    TEST(Json5, TrailingCommentsAnchorToTheirContainer)
    {
        const Json5Value doc = ParseOk("{ a: 1, /* last words */ }");
        ASSERT_EQ(doc.TrailingComments.size(), 1u);
        EXPECT_EQ(doc.TrailingComments.front(), "/* last words */");
        // And they render inside the braces, so a re-parse re-captures them.
        const std::string written = Json5Write(doc);
        EXPECT_LT(written.find("/* last words */"), written.find('}'));
    }

    TEST(Json5, EndOfFileCommentsRoundTripThroughTheSideChannel)
    {
        std::vector<std::string> end;
        Json5ParseError error;
        const std::optional<Json5Value> doc =
            Json5Parse("{ a: 1 }\n// checked by tools\n", &error, &end);
        ASSERT_TRUE(doc.has_value());
        ASSERT_EQ(end.size(), 1u);
        const std::string written = Json5Write(*doc, end);
        EXPECT_NE(written.find("// checked by tools"), std::string::npos);
    }

    TEST(Json5, ShortScalarArraysRenderInline)
    {
        const Json5Value doc = ParseOk("{ position: [0, -0.25, 0] }");
        const std::string written = Json5Write(doc);
        EXPECT_NE(written.find("position: [0, -0.25, 0]"), std::string::npos);
    }

    TEST(Json5, CommentedElementsForceAMultilineArray)
    {
        const Json5Value doc = ParseOk("{ list: [1, /* two */ 2] }");
        const std::string written = Json5Write(doc);
        EXPECT_EQ(written.find("list: [1,"), std::string::npos);
        EXPECT_NE(written.find("/* two */"), std::string::npos);
    }

    TEST(Json5, WriterQuotesNonIdentifierKeys)
    {
        Json5Value doc = Json5Value::MakeObject();
        doc.Members.emplace_back("plain", Json5Value(1.0));
        doc.Members.emplace_back("needs quoting!", Json5Value(2.0));
        const std::string written = Json5Write(doc);
        EXPECT_NE(written.find("plain: 1"), std::string::npos);
        EXPECT_NE(written.find("\"needs quoting!\": 2"), std::string::npos);
    }
} // namespace
