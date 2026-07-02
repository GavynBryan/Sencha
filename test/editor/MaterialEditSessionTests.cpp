#include "MaterialEditSession.h"

#include <assets/material/MaterialLoader.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    class MaterialEditSessionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Dir = std::filesystem::temp_directory_path() / "sencha_material_session_tests";
            std::filesystem::remove_all(Dir);
            std::filesystem::create_directories(Dir);
        }

        void TearDown() override
        {
            std::filesystem::remove_all(Dir);
        }

        std::string WriteSmat(const char* name, const char* json)
        {
            const std::filesystem::path path = Dir / name;
            std::ofstream out(path);
            out << json;
            return path.string();
        }

        std::filesystem::path Dir;
    };
}

TEST_F(MaterialEditSessionTest, OpenLoadsSavedAndWorkingState)
{
    const std::string file =
        WriteSmat("red.smat", R"({"version": 1, "base_color_factor": [1.0, 0.0, 0.0, 1.0]})");

    MaterialEditSession session;
    std::string error;
    ASSERT_TRUE(session.Open("asset://materials/red.smat", file, &error)) << error;

    EXPECT_TRUE(session.HasOpen());
    EXPECT_FALSE(session.IsDirty());
    EXPECT_FLOAT_EQ(session.Working().BaseColorFactor.X, 1.0f);
    EXPECT_FLOAT_EQ(session.Working().BaseColorFactor.Y, 0.0f);
}

TEST_F(MaterialEditSessionTest, OpenFailureKeepsPreviousMaterial)
{
    const std::string good =
        WriteSmat("good.smat", R"({"version": 1})");
    const std::string bad =
        WriteSmat("bad.smat", R"({"version": 1, "unknown_key": true})");

    MaterialEditSession session;
    std::string error;
    ASSERT_TRUE(session.Open("asset://materials/good.smat", good, &error));
    EXPECT_FALSE(session.Open("asset://materials/bad.smat", bad, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(session.VirtualPath(), "asset://materials/good.smat");
}

TEST_F(MaterialEditSessionTest, DirtyTracksDivergenceFromSaved)
{
    const std::string file = WriteSmat("m.smat", R"({"version": 1})");

    MaterialEditSession session;
    ASSERT_TRUE(session.Open("asset://materials/m.smat", file, nullptr));

    MaterialDescription edited = session.Working();
    edited.MetallicFactor = 0.5f;
    session.SetWorking(edited);
    EXPECT_TRUE(session.IsDirty());

    // Editing back to the saved value clears dirty.
    edited.MetallicFactor = 0.0f;
    session.SetWorking(edited);
    EXPECT_FALSE(session.IsDirty());
}

TEST_F(MaterialEditSessionTest, VersionBumpsOnChangeNotOnNoOp)
{
    const std::string file = WriteSmat("m.smat", R"({"version": 1})");

    MaterialEditSession session;
    ASSERT_TRUE(session.Open("asset://materials/m.smat", file, nullptr));
    const uint64_t afterOpen = session.Version();

    session.SetWorking(session.Working());
    EXPECT_EQ(session.Version(), afterOpen);

    MaterialDescription edited = session.Working();
    edited.RoughnessFactor = 0.25f;
    session.SetWorking(edited);
    EXPECT_GT(session.Version(), afterOpen);
}

TEST_F(MaterialEditSessionTest, SaveWritesWorkingAndClearsDirty)
{
    const std::string file = WriteSmat("m.smat", R"({"version": 1})");

    MaterialEditSession session;
    ASSERT_TRUE(session.Open("asset://materials/m.smat", file, nullptr));

    MaterialDescription edited = session.Working();
    edited.RoughnessFactor = 0.3f;
    session.SetWorking(edited);
    ASSERT_TRUE(session.IsDirty());

    std::string error;
    ASSERT_TRUE(session.Save(&error)) << error;
    EXPECT_FALSE(session.IsDirty());

    MaterialDescription onDisk;
    MaterialParseError parseError;
    ASSERT_TRUE(LoadMaterialFromFile(file, onDisk, &parseError)) << parseError.Message;
    EXPECT_FLOAT_EQ(onDisk.RoughnessFactor, 0.3f);
}

TEST_F(MaterialEditSessionTest, SaveToDuplicatesWithoutSwitching)
{
    const std::string file =
        WriteSmat("src.smat", R"({"version": 1, "metallic_factor": 0.8})");

    MaterialEditSession session;
    ASSERT_TRUE(session.Open("asset://materials/src.smat", file, nullptr));

    const std::string copy = (Dir / "copy.smat").string();
    std::string error;
    ASSERT_TRUE(session.SaveTo(copy, &error)) << error;
    EXPECT_EQ(session.FilePath(), file);

    MaterialDescription onDisk;
    ASSERT_TRUE(LoadMaterialFromFile(copy, onDisk, nullptr));
    EXPECT_FLOAT_EQ(onDisk.MetallicFactor, 0.8f);
}

TEST_F(MaterialEditSessionTest, CreateNewWritesDefaults)
{
    const std::string file = (Dir / "fresh.smat").string();
    std::string error;
    ASSERT_TRUE(MaterialEditSession::CreateNew(file, &error)) << error;

    MaterialDescription onDisk;
    ASSERT_TRUE(LoadMaterialFromFile(file, onDisk, nullptr));
    EXPECT_TRUE(SameMaterialDescription(onDisk, MaterialDescription{}));
}
