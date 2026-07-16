#include <assets/material/MaterialLoader.h>
#include <assets/material/MaterialWriter.h>
#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    JsonValue ParseMaterialText(std::string_view text)
    {
        const std::optional<JsonValue> parsed = JsonParse(std::string(text));
        EXPECT_TRUE(parsed.has_value());
        return parsed.value_or(JsonValue{});
    }
}

TEST(MaterialV2, RejectsVersionOne)
{
    MaterialDescription description;
    MaterialParseError error;
    EXPECT_FALSE(ParseMaterialJson(ParseMaterialText(R"({"version": 1})"),
                                   description, &error));
    EXPECT_FALSE(error.Message.empty());
}

TEST(MaterialV2, CurrentVersionUsesStandardLitDefaults)
{
    MaterialDescription description;
    MaterialParseError error;
    ASSERT_TRUE(ParseMaterialJson(ParseMaterialText(R"({"version": 2})"),
                                  description, &error)) << error.Message;

    EXPECT_FLOAT_EQ(description.SpecularIntensity, 0.5f);
    EXPECT_FLOAT_EQ(description.EmissiveStrength, 1.0f);
}

TEST(MaterialV2, ParsesStandardLitFields)
{
    MaterialDescription description;
    MaterialParseError error;
    ASSERT_TRUE(ParseMaterialJson(ParseMaterialText(R"({
        "version": 2,
        "specular_factor": 0.25,
        "emissive_strength": 4.0
    })"), description, &error)) << error.Message;

    EXPECT_FLOAT_EQ(description.SpecularIntensity, 0.25f);
    EXPECT_FLOAT_EQ(description.EmissiveStrength, 4.0f);
}

TEST(MaterialV2, ValidatesStandardLitFieldRanges)
{
    MaterialDescription description;
    EXPECT_FALSE(ParseMaterialJson(ParseMaterialText(R"({
        "version": 2,
        "specular_factor": 1.25
    })"), description));
    EXPECT_FALSE(ParseMaterialJson(ParseMaterialText(R"({
        "version": 2,
        "emissive_strength": -1.0
    })"), description));
}

TEST(MaterialV2, WriterRoundTripsStandardLitFields)
{
    MaterialDescription written;
    written.SpecularIntensity = 0.7f;
    written.EmissiveStrength = 3.5f;

    const JsonValue json = WriteMaterialJson(written);
    const JsonValue* version = json.Find("version");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(static_cast<uint32_t>(version->AsNumber()), kSmatVersion);

    MaterialDescription parsed;
    MaterialParseError error;
    ASSERT_TRUE(ParseMaterialJson(json, parsed, &error)) << error.Message;
    EXPECT_FLOAT_EQ(parsed.SpecularIntensity, 0.7f);
    EXPECT_FLOAT_EQ(parsed.EmissiveStrength, 3.5f);
}
