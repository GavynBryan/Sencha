#include "MaterialTabSet.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    class MaterialTabSetTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Dir = std::filesystem::temp_directory_path() / "sencha_material_tabset_tests";
            std::filesystem::remove_all(Dir);
            std::filesystem::create_directories(Dir);
        }

        void TearDown() override
        {
            std::filesystem::remove_all(Dir);
        }

        std::string WriteSmat(const char* name)
        {
            const std::filesystem::path path = Dir / name;
            std::ofstream out(path);
            out << R"({"version": 1})";
            return path.string();
        }

        static void Edit(MaterialEditTab& tab, float metallic)
        {
            MaterialDescription desc = tab.Session.Working();
            desc.MetallicFactor = metallic;
            tab.Session.SetWorking(desc);
        }

        std::filesystem::path Dir;
    };
}

TEST_F(MaterialTabSetTest, OpenCreatesTabsAndFocusReusesThem)
{
    MaterialTabSet tabs;
    std::string error;
    ASSERT_NE(tabs.OpenOrFocus("asset://materials/a.smat", WriteSmat("a.smat"), &error), nullptr) << error;
    ASSERT_NE(tabs.OpenOrFocus("asset://materials/b.smat", WriteSmat("b.smat"), &error), nullptr) << error;
    EXPECT_EQ(tabs.Tabs().size(), 2u);
    EXPECT_EQ(tabs.ActiveIndex(), 1u);

    // Reopening an already-open material focuses it instead of duplicating.
    ASSERT_NE(tabs.OpenOrFocus("asset://materials/a.smat", "unused", &error), nullptr);
    EXPECT_EQ(tabs.Tabs().size(), 2u);
    EXPECT_EQ(tabs.ActiveIndex(), 0u);
}

TEST_F(MaterialTabSetTest, EachTabKeepsItsOwnDirtyState)
{
    MaterialTabSet tabs;
    tabs.OpenOrFocus("asset://materials/a.smat", WriteSmat("a.smat"), nullptr);
    tabs.OpenOrFocus("asset://materials/b.smat", WriteSmat("b.smat"), nullptr);

    Edit(*tabs.Tabs()[0], 0.5f);
    EXPECT_TRUE(tabs.Tabs()[0]->Session.IsDirty());
    EXPECT_FALSE(tabs.Tabs()[1]->Session.IsDirty());
    EXPECT_TRUE(tabs.AnyDirty());
}

TEST_F(MaterialTabSetTest, CloseClampsTheActiveTab)
{
    MaterialTabSet tabs;
    tabs.OpenOrFocus("asset://materials/a.smat", WriteSmat("a.smat"), nullptr);
    tabs.OpenOrFocus("asset://materials/b.smat", WriteSmat("b.smat"), nullptr);
    ASSERT_EQ(tabs.ActiveIndex(), 1u);

    tabs.Close(1);
    ASSERT_NE(tabs.Active(), nullptr);
    EXPECT_EQ(tabs.Active()->Session.VirtualPath(), "asset://materials/a.smat");

    tabs.Close(0);
    EXPECT_EQ(tabs.Active(), nullptr);
    EXPECT_FALSE(tabs.AnyDirty());
}

TEST_F(MaterialTabSetTest, SaveAllWritesEveryDirtyTab)
{
    MaterialTabSet tabs;
    const std::string fileA = WriteSmat("a.smat");
    const std::string fileB = WriteSmat("b.smat");
    tabs.OpenOrFocus("asset://materials/a.smat", fileA, nullptr);
    tabs.OpenOrFocus("asset://materials/b.smat", fileB, nullptr);

    Edit(*tabs.Tabs()[0], 0.25f);
    Edit(*tabs.Tabs()[1], 0.75f);

    std::string error;
    EXPECT_EQ(tabs.SaveAll(&error), 2);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_FALSE(tabs.AnyDirty());

    // Nothing dirty: nothing rewritten.
    EXPECT_EQ(tabs.SaveAll(&error), 0);
}
