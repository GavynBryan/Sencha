#include "document/CookArtifactTransaction.h"
#include "document/DocumentImportPublisher.h"

#include <assets/cook/CookedCache.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class TempRoot
    {
    public:
        TempRoot()
        {
            std::random_device rd;
            Path = std::filesystem::temp_directory_path()
                 / ("sencha_doc_import_pub_" + std::to_string(rd()));
            std::filesystem::create_directories(Path);
        }

        ~TempRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }

        std::filesystem::path Path;
    };

    std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }
} // namespace

// The publisher stages import bytes through the transaction (not active until
// commit) and applies index entries to the caller-owned index, so a document
// cook publishes referenced-asset imports and its own entry as one change.
TEST(DocumentImportPublisher, StagesBytesThroughTransactionAndCommitsWithIndex)
{
    TempRoot root;
    CookedCacheIndex index;

    {
        CookArtifactTransaction transaction(root.Path, "levels/test.sscene");
        DocumentImportPublisher publisher(transaction, root.Path, index);

        const std::vector<std::byte> bytes = Bytes("cooked stex bytes");
        ASSERT_TRUE(publisher.WriteBytes(".cooked/textures/dev/checker.stex", bytes));
        publisher.PutIndexEntry(CookedSourceEntry{
            .SourceRelPath = "textures/dev/checker.png",
            .InputFingerprint = 7,
            .Artifacts = { { "asset://textures/dev/checker.stex",
                             ".cooked/textures/dev/checker.stex",
                             AssetType::Texture, 42 } } });

        // Bytes are staged, not active; the index entry is applied immediately
        // to the caller's index.
        EXPECT_FALSE(std::filesystem::exists(
            root.Path / ".cooked/textures/dev/checker.stex"));
        EXPECT_NE(index.Find("textures/dev/checker.png"), nullptr);

        std::string error;
        ASSERT_TRUE(transaction.Commit(&error)) << error;
    }

    // After commit the artifact is active and the index carries its entry.
    EXPECT_TRUE(std::filesystem::is_regular_file(
        root.Path / ".cooked/textures/dev/checker.stex"));
    const CookedSourceEntry* entry = index.Find("textures/dev/checker.png");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->Artifacts.size(), 1u);
    EXPECT_EQ(entry->Artifacts[0].FileRelPath, ".cooked/textures/dev/checker.stex");
}

// A transaction abandoned before commit leaves no active import.
TEST(DocumentImportPublisher, AbandonedTransactionPublishesNothing)
{
    TempRoot root;
    CookedCacheIndex index;

    {
        CookArtifactTransaction transaction(root.Path, "levels/test.sscene");
        DocumentImportPublisher publisher(transaction, root.Path, index);
        const std::vector<std::byte> bytes = Bytes("cooked stex bytes");
        ASSERT_TRUE(publisher.WriteBytes(".cooked/textures/dev/checker.stex", bytes));
        // Transaction destructs without Commit: staged output is discarded.
    }

    EXPECT_FALSE(std::filesystem::exists(
        root.Path / ".cooked/textures/dev/checker.stex"));
}
