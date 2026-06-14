#include <core/hash/ContentHash.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// -- Known XXH64 vectors (seed 0) -------------------------------------------
//
// Published reference values; the fox sentence is 43 bytes, so it exercises
// the >= 32-byte main loop, not just the tail.

TEST(ContentHash, EmptyInput)
{
    EXPECT_EQ(HashBytes64(std::string_view{}), 0xEF46DB3751D8E999ULL);
}

TEST(ContentHash, SingleByte)
{
    EXPECT_EQ(HashBytes64(std::string_view{ "a" }), 0xD24EC4F1A98C6E5BULL);
}

TEST(ContentHash, ShortInput)
{
    EXPECT_EQ(HashBytes64(std::string_view{ "abc" }), 0x44BC2CF5AD770999ULL);
}

TEST(ContentHash, LongInputMainLoop)
{
    EXPECT_EQ(HashBytes64(std::string_view{ "The quick brown fox jumps over the lazy dog" }),
              0x0B242D361FDA71BCULL);
}

// -- Properties --------------------------------------------------------------

TEST(ContentHash, SeedChangesHash)
{
    const std::string_view text = "same bytes, different seed";
    EXPECT_NE(HashBytes64(text, 0), HashBytes64(text, 1));
}

TEST(ContentHash, SingleBitChangeChangesHash)
{
    std::vector<std::byte> bytes(64, std::byte{ 0x5A });
    const uint64_t before = HashBytes64(bytes);
    bytes[40] ^= std::byte{ 0x01 };
    EXPECT_NE(before, HashBytes64(bytes));
}

TEST(ContentHash, StringAndByteOverloadsAgree)
{
    const std::string_view text = "overload agreement";
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    EXPECT_EQ(HashBytes64(text), HashBytes64(bytes));
}

// -- HashFileContents ---------------------------------------------------------

TEST(ContentHash, FileHashMatchesByteHash)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_content_hash_test.bin";
    const std::string contents = "file bytes for hashing\x00\x01\x02";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    uint64_t fileHash = 0;
    ASSERT_TRUE(HashFileContents(path.generic_string(), fileHash));
    EXPECT_EQ(fileHash, HashBytes64(std::string_view(contents)));

    std::filesystem::remove(path);
}

TEST(ContentHash, FileHashFailsOnMissingFile)
{
    uint64_t hash = 42;
    EXPECT_FALSE(HashFileContents("/nonexistent/sencha/content/hash.bin", hash));
    EXPECT_EQ(hash, 42u);
}
