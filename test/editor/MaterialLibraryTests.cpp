#include "project/MaterialLibrary.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    class MaterialLibraryTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Base = std::filesystem::temp_directory_path() / "sencha_material_library_tests";
            std::filesystem::remove_all(Base);
        }

        void TearDown() override
        {
            std::filesystem::remove_all(Base);
        }

        std::string MakeRoot(const char* name, const std::vector<std::string>& smatRelPaths)
        {
            const std::filesystem::path root = Base / name;
            for (const std::string& rel : smatRelPaths)
            {
                const std::filesystem::path file = root / rel;
                std::filesystem::create_directories(file.parent_path());
                std::ofstream out(file);
                out << "{}";
            }
            std::filesystem::create_directories(root);
            return root.string();
        }

        std::filesystem::path Base;
        LoggingProvider Logging;
    };
}

TEST_F(MaterialLibraryTest, ScansAllRootsIntoOneSortedList)
{
    const std::vector<std::string> roots{
        MakeRoot("core", { "materials/dev/gray.smat", "materials/dev/orange.smat" }),
        MakeRoot("dlc", { "materials/stone/wall.smat" }),
    };

    MaterialLibrary library(Logging);
    library.Rescan(roots);

    const auto& materials = library.Materials();
    ASSERT_EQ(materials.size(), 3u);
    EXPECT_EQ(materials[0].Path, "asset://materials/dev/gray.smat");
    EXPECT_EQ(materials[1].Path, "asset://materials/dev/orange.smat");
    EXPECT_EQ(materials[2].Path, "asset://materials/stone/wall.smat");
    EXPECT_EQ(materials[2].DisplayName, "materials/stone/wall");
}

TEST_F(MaterialLibraryTest, DuplicateRelativePathsListOnce)
{
    // Same root-relative path in two roots resolves to one asset:// ref at load
    // time; the pickable list must not show it twice.
    const std::vector<std::string> roots{
        MakeRoot("a", { "materials/shared.smat" }),
        MakeRoot("b", { "materials/shared.smat" }),
    };

    MaterialLibrary library(Logging);
    library.Rescan(roots);
    ASSERT_EQ(library.Materials().size(), 1u);
    EXPECT_EQ(library.Materials()[0].Path, "asset://materials/shared.smat");
}

TEST_F(MaterialLibraryTest, RescanWithOwnRootsIsStable)
{
    const std::vector<std::string> roots{ MakeRoot("core", { "materials/a.smat" }) };

    MaterialLibrary library(Logging);
    library.Rescan(roots);
    ASSERT_EQ(library.Materials().size(), 1u);

    // The panel's Rescan button passes the library's own scanned roots back in.
    library.Rescan(library.Roots());
    ASSERT_EQ(library.Materials().size(), 1u);
    ASSERT_EQ(library.Roots().size(), 1u);
    EXPECT_EQ(library.Roots()[0], roots[0]);
}

TEST_F(MaterialLibraryTest, MissingAndEmptyRootsContributeNothing)
{
    const std::vector<std::string> roots{
        std::string(),
        (Base / "does_not_exist").string(),
    };

    MaterialLibrary library(Logging);
    library.Rescan(roots);
    EXPECT_TRUE(library.Materials().empty());
}
