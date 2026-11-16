#include <core/io/FileBytes.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
    class FileBytes : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Root = std::filesystem::temp_directory_path()
                / ("sencha_file_bytes_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root);
        }

        void TearDown() override { std::filesystem::remove_all(Root); }

        static std::string Read(const std::filesystem::path& path)
        {
            std::vector<std::byte> bytes;
            EXPECT_TRUE(ReadFileBytes(path, bytes));
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        std::filesystem::path Root;
    };
}

TEST_F(FileBytes, WhatIsWrittenReadsBack)
{
    const std::filesystem::path path = Root / "round_trip.bin";
    const std::vector<std::byte> bytes{ std::byte{ 1 }, std::byte{ 0 }, std::byte{ 255 } };
    ASSERT_TRUE(WriteFileBytes(path, bytes));
    std::vector<std::byte> read;
    ASSERT_TRUE(ReadFileBytes(path, read));
    EXPECT_EQ(read, bytes);
}

TEST_F(FileBytes, AWriteReplacesTheWholeFileAndLeavesNothingBesideIt)
{
    const std::filesystem::path path = Root / "settings.json";
    ASSERT_TRUE(WriteFileText(path, "a much longer first version"));
    ASSERT_TRUE(WriteFileText(path, "short"));
    EXPECT_EQ(Read(path), "short");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Root), std::filesystem::directory_iterator{}), 1);
}

TEST_F(FileBytes, AWriteThatCannotFinishLeavesTheOldFileIntact)
{
    const std::filesystem::path path = Root / "save.bin";
    ASSERT_TRUE(WriteFileText(path, "kept"));
    std::filesystem::path staging = path;
    staging += ".tmp";
    std::filesystem::create_directories(staging);

    EXPECT_FALSE(WriteFileText(path, "lost"));
    EXPECT_EQ(Read(path), "kept");
}

TEST_F(FileBytes, AnEmptyWriteLeavesAnEmptyFile)
{
    const std::filesystem::path path = Root / "empty.bin";
    ASSERT_TRUE(WriteFileText(path, "something"));
    ASSERT_TRUE(WriteFileBytes(path, {}));
    std::vector<std::byte> read{ std::byte{ 9 } };
    ASSERT_TRUE(ReadFileBytes(path, read));
    EXPECT_TRUE(read.empty());
}
