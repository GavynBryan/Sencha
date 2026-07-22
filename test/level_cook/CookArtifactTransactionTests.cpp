#include "document/CookArtifactTransaction.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}

TEST(CookArtifactTransactionTest, PublishesReplacementsAndWithdrawalsTogether)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("sencha_cook_tx_" + std::to_string(reinterpret_cast<std::uintptr_t>(
            ::testing::UnitTest::GetInstance())));
    fs::remove_all(root);
    fs::create_directories(root / ".cooked/levels/test");
    const fs::path mesh = root / ".cooked/levels/test/cell.smesh";
    const fs::path light = root / ".cooked/levels/test/lightmap.stex";
    std::ofstream(mesh) << "old mesh";
    std::ofstream(light) << "old light";

    CookArtifactTransaction transaction(root, "levels/test");
    std::ofstream(transaction.Stage(mesh)) << "new mesh";
    transaction.Withdraw(light);
    std::string error;
    ASSERT_TRUE(transaction.Commit(&error)) << error;
    EXPECT_EQ(Read(mesh), "new mesh");
    EXPECT_FALSE(fs::exists(light));

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(CookArtifactTransactionTest, MissingStagedFileLeavesActiveFilesUntouched)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("sencha_cook_tx_failure_" + std::to_string(reinterpret_cast<std::uintptr_t>(
            ::testing::UnitTest::GetInstance())));
    fs::remove_all(root);
    fs::create_directories(root / ".cooked/levels/test");
    const fs::path first = root / ".cooked/levels/test/first";
    const fs::path second = root / ".cooked/levels/test/second";
    std::ofstream(first) << "old first";
    std::ofstream(second) << "old second";

    CookArtifactTransaction transaction(root, "levels/test");
    std::ofstream(transaction.Stage(first)) << "new first";
    (void)transaction.Stage(second);
    std::string error;
    EXPECT_FALSE(transaction.Commit(&error));
    EXPECT_EQ(Read(first), "old first");
    EXPECT_EQ(Read(second), "old second");

    std::error_code ec;
    fs::remove_all(root, ec);
}

