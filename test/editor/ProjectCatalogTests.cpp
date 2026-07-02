#include "ProjectCatalog.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    class ProjectCatalogTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Dir = std::filesystem::temp_directory_path() / "sencha_project_catalog_tests";
            std::filesystem::remove_all(Dir);
            std::filesystem::create_directories(Dir);
            File = Dir / "recent_projects.json";
        }

        void TearDown() override
        {
            std::filesystem::remove_all(Dir);
        }

        std::filesystem::path Dir;
        std::filesystem::path File;
    };
}

TEST_F(ProjectCatalogTest, MissingFileIsAnEmptyCatalog)
{
    ProjectCatalog catalog;
    std::string error;
    EXPECT_TRUE(catalog.Load(File, &error)) << error;
    EXPECT_TRUE(catalog.Entries().empty());
}

TEST_F(ProjectCatalogTest, SaveLoadRoundTripsInOrder)
{
    ProjectCatalog catalog;
    catalog.Touch("/projects/a/project.senchaproj", "A");
    catalog.Touch("/projects/b/project.senchaproj", "B");

    std::string error;
    ASSERT_TRUE(catalog.Save(File, &error)) << error;

    ProjectCatalog loaded;
    ASSERT_TRUE(loaded.Load(File, &error)) << error;
    ASSERT_EQ(loaded.Entries().size(), 2u);
    EXPECT_EQ(loaded.Entries()[0].Path, "/projects/b/project.senchaproj");
    EXPECT_EQ(loaded.Entries()[0].Name, "B");
    EXPECT_EQ(loaded.Entries()[1].Path, "/projects/a/project.senchaproj");
}

TEST_F(ProjectCatalogTest, TouchMovesToFrontAndRefreshesName)
{
    ProjectCatalog catalog;
    catalog.Touch("/p/a.senchaproj", "A");
    catalog.Touch("/p/b.senchaproj", "B");
    catalog.Touch("/p/a.senchaproj", "A renamed");

    ASSERT_EQ(catalog.Entries().size(), 2u);
    EXPECT_EQ(catalog.Entries()[0].Path, "/p/a.senchaproj");
    EXPECT_EQ(catalog.Entries()[0].Name, "A renamed");
}

TEST_F(ProjectCatalogTest, RemoveDropsTheEntry)
{
    ProjectCatalog catalog;
    catalog.Touch("/p/a.senchaproj", "A");
    catalog.Touch("/p/b.senchaproj", "B");
    catalog.Remove("/p/a.senchaproj");

    ASSERT_EQ(catalog.Entries().size(), 1u);
    EXPECT_EQ(catalog.Entries()[0].Path, "/p/b.senchaproj");
}

TEST_F(ProjectCatalogTest, MalformedFileFailsWithoutCrashing)
{
    {
        std::ofstream out(File);
        out << "not json {";
    }

    ProjectCatalog catalog;
    std::string error;
    EXPECT_FALSE(catalog.Load(File, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(catalog.Entries().empty());
}

TEST_F(ProjectCatalogTest, CapsTheListLength)
{
    ProjectCatalog catalog;
    for (int i = 0; i < 30; ++i)
        catalog.Touch("/p/" + std::to_string(i) + ".senchaproj", "P");
    EXPECT_EQ(catalog.Entries().size(), 20u);
    EXPECT_EQ(catalog.Entries()[0].Path, "/p/29.senchaproj");
}
