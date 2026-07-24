#include "document/CookReceipt.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

TEST(CookReceiptTest, RoundTripsStepIdentityArtifactsAndMetadata)
{
    DocumentCookReceipt source;
    source.Target = "levels/test.json";
    source.PublishedProfileId = "full";
    source.PublishedFingerprint = 0x1234567890abcdefULL;
    source.PublishedOutputFamilies = { "structure", "direct_lightmap" };
    source.Steps.push_back(CookStepReceipt{
        .StepId = "direct_lightmap",
        .Version = 3,
        .InputFingerprint = 0xfedcba0987654321ULL,
        .Dependencies = { { "lightmap_surfaces", 42 } },
        .Artifacts = {
            CookedArtifact{ "asset://levels/test/lightmap.stex",
                            ".cooked/levels/test/lightmap.stex",
                            AssetType::Texture, 99 },
        },
        .Metadata = JsonValue(JsonValue::Object{ { "width", JsonValue(256.0) } }),
        .DurationMilliseconds = 12.5,
    });

    DocumentCookReceipt parsed;
    std::string error;
    ASSERT_TRUE(ReadDocumentCookReceipt(WriteDocumentCookReceipt(source), parsed, &error))
        << error;
    ASSERT_EQ(parsed.Steps.size(), 1u);
    EXPECT_EQ(parsed.PublishedFingerprint, source.PublishedFingerprint);
    EXPECT_EQ(parsed.Steps.front().InputFingerprint,
              source.Steps.front().InputFingerprint);
    EXPECT_EQ(parsed.Steps.front().Artifacts.front().ContentHash, 99u);
    ASSERT_NE(parsed.Steps.front().Metadata.Find("width"), nullptr);
    EXPECT_EQ(parsed.Steps.front().Metadata.Find("width")->AsNumber(), 256.0);
}

TEST(CookReceiptTest, PutReplacesOnlyMatchingStep)
{
    DocumentCookReceipt receipt;
    PutCookStepReceipt(receipt, CookStepReceipt{ .StepId = "a", .Version = 1 });
    PutCookStepReceipt(receipt, CookStepReceipt{ .StepId = "b", .Version = 1 });
    PutCookStepReceipt(receipt, CookStepReceipt{ .StepId = "a", .Version = 2 });

    ASSERT_EQ(receipt.Steps.size(), 2u);
    ASSERT_NE(FindCookStepReceipt(receipt, "a"), nullptr);
    EXPECT_EQ(FindCookStepReceipt(receipt, "a")->Version, 2u);
    EXPECT_EQ(FindCookStepReceipt(receipt, "b")->Version, 1u);
}

