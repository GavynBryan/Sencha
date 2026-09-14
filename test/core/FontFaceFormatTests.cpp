#include <gtest/gtest.h>

#include <assets/cook/FontImportSettings.h>
#include <assets/font/FontFace.h>
#include <assets/font/FontFaceFormat.h>
#include <assets/font/FontFaceSerializer.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

FontFace MakeFace()
{
    FontFace face;
    face.Family = "Inter";
    face.Style = FontStyle::Italic;
    face.Weight = kFontWeightBold;
    face.Fallback = true;
    // Stand-in for face bytes: the cook passes them through unchanged, so what
    // they contain is irrelevant to the container's contract. Built explicitly
    // rather than from a literal, so the embedded NULs of a real font header
    // survive instead of terminating a string_view.
    face.Bytes = { std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                   std::byte{'g'}, std::byte{'l'}, std::byte{'y'}, std::byte{'f'},
                   std::byte{0x00}, std::byte{0xFF} };
    return face;
}
} // namespace

TEST(SfontFormat, FaceRoundTripsIncludingItsRegistrationMetadata)
{
    const FontFace original = MakeFace();

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSfontToBytes(original, bytes));
    EXPECT_TRUE(LooksLikeSfont(bytes.data(), bytes.size()));

    FontFace parsed;
    std::string error;
    ASSERT_TRUE(LoadSfontFromBytes(bytes, parsed, &error)) << error;

    EXPECT_EQ(parsed.Family, original.Family);
    EXPECT_EQ(parsed.Style, original.Style);
    EXPECT_EQ(parsed.Weight, original.Weight);
    EXPECT_EQ(parsed.Fallback, original.Fallback);
    EXPECT_EQ(parsed.Bytes, original.Bytes)
        << "the cook passes face bytes through; a byte changed here is a broken glyph";
}

TEST(SfontFormat, AFaceWithNoFamilyOrNoBytesIsRefused)
{
    std::vector<std::byte> bytes;

    FontFace noFamily = MakeFace();
    noFamily.Family.clear();
    EXPECT_FALSE(WriteSfontToBytes(noFamily, bytes));

    FontFace noBytes = MakeFace();
    noBytes.Bytes.clear();
    EXPECT_FALSE(WriteSfontToBytes(noBytes, bytes));
}

TEST(SfontFormat, TruncationAtEveryLengthIsRejected)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSfontToBytes(MakeFace(), bytes));

    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        FontFace parsed;
        EXPECT_FALSE(LoadSfontFromBytes(std::span<const std::byte>(bytes.data(), length), parsed))
            << "a " << length << "-byte prefix parsed as a whole face";
    }
}

TEST(SfontFormat, ForeignBytesAndUnknownVersionsAreRejected)
{
    const std::vector<std::byte> raw = BytesOf("OTTO and then some actual font bytes");
    FontFace parsed;
    EXPECT_FALSE(LoadSfontFromBytes(raw, parsed));

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSfontToBytes(MakeFace(), bytes));
    SfontFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    header.Version = kSfontVersion + 1;
    std::memcpy(bytes.data(), &header, sizeof(header));
    EXPECT_FALSE(LoadSfontFromBytes(bytes, parsed));
}

// -- filename convention -----------------------------------------------------

namespace
{
struct InferenceCase
{
    std::string_view Path;
    std::string_view Family;
    std::uint16_t Weight;
    FontStyle Style;
};
} // namespace

TEST(FontFileNameConvention, VendorSuffixesResolveToFamilyWeightAndStyle)
{
    constexpr InferenceCase kCases[] = {
        { "ui/Inter-Regular.ttf",          "Inter",         400, FontStyle::Normal },
        { "ui/Inter-Bold.ttf",             "Inter",         700, FontStyle::Normal },
        { "ui/Inter-BoldItalic.ttf",       "Inter",         700, FontStyle::Italic },
        { "ui/Inter-Italic.otf",           "Inter",         400, FontStyle::Italic },
        { "ui/JetBrainsMono_SemiBold.ttf", "JetBrainsMono", 600, FontStyle::Normal },
        { "ui/Chakra-Black.ttf",           "Chakra",        900, FontStyle::Normal },
        { "Inter-Thin.ttf",                "Inter",         100, FontStyle::Normal },
    };

    for (const InferenceCase& testCase : kCases)
    {
        std::string family;
        std::uint16_t weight = 0;
        FontStyle style = FontStyle::Normal;
        InferFontFaceFromFileName(testCase.Path, family, weight, style);

        EXPECT_EQ(family, testCase.Family) << testCase.Path;
        EXPECT_EQ(weight, testCase.Weight) << testCase.Path;
        EXPECT_EQ(style, testCase.Style) << testCase.Path;
    }
}

TEST(FontFileNameConvention, AnUnrecognisedSuffixStaysPartOfTheFamily)
{
    // "Inter-Display" is a different family, not a weight. Dropping the suffix
    // would file two distinct faces under one name and let either win.
    std::string family;
    std::uint16_t weight = 0;
    FontStyle style = FontStyle::Italic;
    InferFontFaceFromFileName("ui/Inter-Display.ttf", family, weight, style);

    EXPECT_EQ(family, "Inter-Display");
    EXPECT_EQ(weight, kFontWeightNormal);
    EXPECT_EQ(style, FontStyle::Normal);
}

TEST(FontFileNameConvention, ANameWithNoSuffixIsTheFamilyWholesale)
{
    std::string family;
    std::uint16_t weight = 0;
    FontStyle style = FontStyle::Italic;
    InferFontFaceFromFileName("ui/Chakra.ttf", family, weight, style);

    EXPECT_EQ(family, "Chakra");
    EXPECT_EQ(weight, kFontWeightNormal);
    EXPECT_EQ(style, FontStyle::Normal);
}

// -- sidecar -----------------------------------------------------------------

TEST(FontImportSettings, AMissingSidecarIsTheConventionNotAnError)
{
    FontImportSettings settings;
    std::string error;
    ASSERT_TRUE(ParseFontImportSettings({}, settings, &error)) << error;
    EXPECT_FALSE(settings.Family.has_value());
    EXPECT_FALSE(settings.Weight.has_value());
    EXPECT_FALSE(settings.Style.has_value());
    EXPECT_FALSE(settings.Fallback.has_value());
}

TEST(FontImportSettings, EachFieldOverridesIndependently)
{
    const std::vector<std::byte> json = BytesOf(
        R"({"version":1,"family":"Kyusu Display","weight":650,"style":"italic","fallback":true})");

    FontImportSettings settings;
    std::string error;
    ASSERT_TRUE(ParseFontImportSettings(json, settings, &error)) << error;

    ASSERT_TRUE(settings.Family.has_value());
    EXPECT_EQ(*settings.Family, "Kyusu Display");
    ASSERT_TRUE(settings.Weight.has_value());
    EXPECT_EQ(*settings.Weight, 650);
    ASSERT_TRUE(settings.Style.has_value());
    EXPECT_EQ(*settings.Style, FontStyle::Italic);
    ASSERT_TRUE(settings.Fallback.has_value());
    EXPECT_TRUE(*settings.Fallback);
}

TEST(FontImportSettings, ATypoFailsRatherThanCookingWithTheGuess)
{
    // A sidecar exists because somebody meant something specific by it. Falling
    // back to the filename here would cook a face under a name nobody asked for
    // and report success.
    struct BadCase { std::string_view Json; const char* What; };
    constexpr BadCase kCases[] = {
        { R"({"weight":"bold"})",   "weight as a string" },
        { R"({"weight":0})",        "weight below the CSS scale" },
        { R"({"weight":1001})",     "weight above the CSS scale" },
        { R"({"style":"oblique"})", "a style the engine has no concept of" },
        { R"({"family":""})",       "an empty family" },
        { R"({"fallback":"yes"})",  "fallback as a string" },
        { R"(not json at all)",     "unparsable bytes" },
        { R"([1,2,3])",             "a non-object root" },
    };

    for (const BadCase& testCase : kCases)
    {
        FontImportSettings settings;
        std::string error;
        EXPECT_FALSE(ParseFontImportSettings(BytesOf(testCase.Json), settings, &error))
            << "accepted " << testCase.What;
        EXPECT_FALSE(error.empty()) << testCase.What;
    }
}
