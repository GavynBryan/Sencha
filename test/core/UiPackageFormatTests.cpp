#include <gtest/gtest.h>

#include <assets/ui/UiPackage.h>
#include <cstring>
#include <assets/ui/UiPackageFormat.h>
#include <assets/ui/UiPackageSerializer.h>

#include <string>
#include <vector>

// The .sui contract: a package that round trips is a package that can be opened
// without touching the filesystem, and a malformed one is rejected rather than
// half-read. A half-read document is worse than a missing one -- it draws.

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "hud.rml";

    UiPackageBlob root;
    root.VirtualName = "hud.rml";
    root.SourcePath = "ui/hud.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf("<rml><body>health</body></rml>");
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "theme.rcss";
    sheet.SourcePath = "ui/theme.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf("body { color: #fff; }");
    package.Blobs.push_back(std::move(sheet));

    package.Resources.push_back(AssetRef{ AssetType::Font, "asset://ui/Inter-Regular.ttf" });
    package.Resources.push_back(AssetRef{ AssetType::Texture, "asset://ui/panel.png" });

    package.Unsupported.push_back(UiUnsupportedFeature{ "box-shadow", "ui/theme.rcss", 12 });
    return package;
}
} // namespace

TEST(SuiFormat, PackageRoundTripsWholeInsteadOfApproximately)
{
    const UiPackage original = MakePackage();

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(original, bytes));
    EXPECT_TRUE(LooksLikeSui(bytes.data(), bytes.size()));

    UiPackage parsed;
    std::string error;
    ASSERT_TRUE(LoadSuiFromBytes(bytes, parsed, &error)) << error;

    EXPECT_EQ(parsed.RootDocumentName, original.RootDocumentName);

    ASSERT_EQ(parsed.Blobs.size(), original.Blobs.size());
    for (std::size_t i = 0; i < parsed.Blobs.size(); ++i)
    {
        EXPECT_EQ(parsed.Blobs[i].VirtualName, original.Blobs[i].VirtualName);
        EXPECT_EQ(parsed.Blobs[i].SourcePath, original.Blobs[i].SourcePath);
        EXPECT_EQ(parsed.Blobs[i].Kind, original.Blobs[i].Kind);
        EXPECT_EQ(parsed.Blobs[i].Bytes, original.Blobs[i].Bytes);
    }

    ASSERT_EQ(parsed.Resources.size(), 2u);
    EXPECT_EQ(parsed.Resources[0].Type, AssetType::Font);
    EXPECT_EQ(parsed.Resources[0].Path, "asset://ui/Inter-Regular.ttf");
    EXPECT_EQ(parsed.Resources[1].Type, AssetType::Texture);

    ASSERT_EQ(parsed.Unsupported.size(), 1u);
    EXPECT_EQ(parsed.Unsupported[0].Feature, "box-shadow");
    EXPECT_EQ(parsed.Unsupported[0].SourcePath, "ui/theme.rcss");
    EXPECT_EQ(parsed.Unsupported[0].Line, 12u);
}

TEST(SuiFormat, TheSameSourcesProduceTheSameBytes)
{
    // The cooked-cache hash is taken over these bytes, so a package that
    // serialized differently run to run would re-cook and re-publish forever.
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), first));
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), second));
    EXPECT_EQ(first, second);
}

TEST(SuiFormat, ARootNamingNoBlobIsRefusedAtWriteAndAtRead)
{
    UiPackage package = MakePackage();
    package.RootDocumentName = "somewhere-else.rml";

    std::vector<std::byte> bytes;
    EXPECT_FALSE(WriteSuiToBytes(package, bytes))
        << "a package that could not be opened must not be written";

    // And the reader does not trust a container that claims it anyway.
    std::vector<std::byte> valid;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), valid));
    UiPackage parsed;
    ASSERT_TRUE(LoadSuiFromBytes(valid, parsed));
    EXPECT_NE(parsed.FindBlob(parsed.RootDocumentName), nullptr);
}

TEST(SuiFormat, EmptyPackagesAreRefused)
{
    std::vector<std::byte> bytes;
    EXPECT_FALSE(WriteSuiToBytes(UiPackage{}, bytes));
}

TEST(SuiFormat, ForeignBytesAreRejectedNotGuessedAt)
{
    const std::vector<std::byte> notAPackage = BytesOf("<rml><body>loose markup</body></rml>");
    EXPECT_FALSE(LooksLikeSui(notAPackage.data(), notAPackage.size()));

    UiPackage parsed;
    std::string error;
    EXPECT_FALSE(LoadSuiFromBytes(notAPackage, parsed, &error));
    EXPECT_FALSE(error.empty());
}

TEST(SuiFormat, TruncationAtEveryLengthIsRejectedRatherThanPartiallyRead)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));

    // Every prefix short of the whole container must fail. This is the test that
    // would catch a bounds check written against the wrong cursor.
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        const std::span<const std::byte> prefix(bytes.data(), length);
        UiPackage parsed;
        EXPECT_FALSE(LoadSuiFromBytes(prefix, parsed))
            << "a " << length << "-byte prefix parsed as a whole package";
    }
}

TEST(SuiFormat, AnUnknownVersionIsRefusedRatherThanReadAsVersionOne)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));

    SuiFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    header.Version = kSuiVersion + 1;
    std::memcpy(bytes.data(), &header, sizeof(header));

    UiPackage parsed;
    std::string error;
    EXPECT_FALSE(LoadSuiFromBytes(bytes, parsed, &error));
    EXPECT_NE(error.find("version"), std::string::npos) << error;
}

TEST(SuiFormat, AResourceNamingAnUnknownAssetTypeIsRejected)
{
    UiPackage package = MakePackage();
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(package, bytes));

    // The resource table stores AssetType as a raw u16. A container written by a
    // newer cook naming a kind this build has never heard of must not resolve to
    // whatever happens to sit at that value.
    const auto sentinel = static_cast<std::uint16_t>(AssetType::Count);
    bool patched = false;
    for (std::size_t i = 0; i + sizeof(std::uint16_t) <= bytes.size(); ++i)
    {
        std::uint16_t candidate = 0;
        std::memcpy(&candidate, bytes.data() + i, sizeof(candidate));
        if (candidate != static_cast<std::uint16_t>(AssetType::Font))
            continue;
        std::memcpy(bytes.data() + i, &sentinel, sizeof(sentinel));
        patched = true;
        break;
    }
    ASSERT_TRUE(patched);

    UiPackage parsed;
    std::string error;
    EXPECT_FALSE(LoadSuiFromBytes(bytes, parsed, &error));
}

TEST(SuiFormat, FindBlobResolvesByTheNameMarkupUses)
{
    const UiPackage package = MakePackage();
    ASSERT_NE(package.FindBlob("theme.rcss"), nullptr);
    EXPECT_EQ(package.FindBlob("theme.rcss")->Kind, UiBlobKind::StyleSheet);
    EXPECT_EQ(package.FindBlob("ui/theme.rcss"), nullptr)
        << "lookup is by virtual name, not by source path";
    EXPECT_EQ(package.FindBlob("absent.rcss"), nullptr);
}
