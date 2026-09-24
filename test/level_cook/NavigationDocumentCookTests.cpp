// End-to-end: an authored brush level with navigation settings cooks a zone
// navigation file beside its scene, from the same cells the collision bake
// reads. Headless: no asset system, no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationFile.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

constexpr std::string_view kSettings = R"({
    "type": "navigation.settings", "version": 1,
    "data": {
        "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3,
                        "height": 1.8, "max_slope_degrees": 45, "max_climb": 0.35,
                        "cell_size": 0.2, "cell_height": 0.1, "tile_cells": 40 } ],
        "areas": [] } })";

std::vector<std::byte> ReadBytes(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

class NavigationDocumentCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_navcook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        const fs::path material = Root / "materials/dev/gray.smat";
        fs::create_directories(material.parent_path());
        std::ofstream(material, std::ios::trunc) << "{}";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    void WriteSettings()
    {
        const fs::path path = Root / "data/navigation.sdata";
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::trunc) << kSettings;
    }

    // Two floor slabs 4 m apart; optionally one link across the gap.
    fs::path AuthorLevel(std::optional<Vec3d> linkExit, NavLinkId linkId = NavLinkId{ 0xabcdef })
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 5, -0.5f, 5 }, Vec3d{ 5, 0.5f, 5 });
        doc.GetScene().CreateBrush(Vec3d{ 19, -0.5f, 5 }, Vec3d{ 5, 0.5f, 5 });
        if (linkExit.has_value())
        {
            const Vec3d entry(9.0f, 0.0f, 5.0f);
            const EntityId entity = doc.GetScene().CreateEntity(entry);
            NavLink link;
            link.Id = linkId;
            link.Traversal = NavTraversalName("navigation.traversal.jump");
            link.ExitOffset = *linkExit - entry;
            doc.GetScene().GetRegistry().Components.AddComponent(entity, link);
        }
        const fs::path levelPath = Root / "levels/nav.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    DocumentCookResult Cook(const fs::path& levelPath, bool force = false)
    {
        DocumentCookOptions options;
        options.ForceRebuild = force;
        return CookDocument(levelPath, Root, 16.0, nullptr, nullptr, {}, {}, options);
    }

    fs::path NavigationPath() const { return Root / ".cooked/levels/nav/navigation.snav"; }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(NavigationDocumentCookTest, CooksNavigationBesideTheScene)
{
    WriteSettings();
    const DocumentCookResult result = Cook(AuthorLevel(Vec3d(15.0f, 0.0f, 5.0f)));
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_TRUE(result.Diagnostics.empty());
    EXPECT_GT(result.NavigationTileCount, 0u);
    ASSERT_TRUE(fs::exists(NavigationPath()));

    NavigationFile file;
    std::string error;
    ASSERT_TRUE(DecodeNavigationFile(ReadBytes(NavigationPath()), file, &error)) << error;
    ASSERT_EQ(file.Profiles.size(), 1u);
    EXPECT_FALSE(file.Positions.empty());
    ASSERT_EQ(file.Links.size(), 1u);
    EXPECT_EQ(file.Links[0].Id, NavLinkId{ 0xabcdef });
    EXPECT_EQ(file.Links[0].Traversal, "navigation.traversal.jump");
    EXPECT_NEAR(file.Links[0].Exit.X, 15.0f, 1e-4f);
}

TEST_F(NavigationDocumentCookTest, ForcedRecookIsByteIdentical)
{
    WriteSettings();
    const fs::path level = AuthorLevel(Vec3d(15.0f, 0.0f, 5.0f));
    ASSERT_TRUE(Cook(level, true).Success);
    const std::vector<std::byte> first = ReadBytes(NavigationPath());
    ASSERT_TRUE(Cook(level, true).Success);
    EXPECT_EQ(ReadBytes(NavigationPath()), first);
}

TEST_F(NavigationDocumentCookTest, UnreachableLinkFailsTheCookNamingTheLink)
{
    WriteSettings();
    const DocumentCookResult result = Cook(AuthorLevel(Vec3d(60.0f, 0.0f, 60.0f)));
    EXPECT_FALSE(result.Success);
    const auto found = std::find_if(result.Diagnostics.begin(), result.Diagnostics.end(),
        [](const CookDiagnostic& d) { return d.Rule == "nav.link.exit_unprojected"; });
    ASSERT_NE(found, result.Diagnostics.end());
    EXPECT_EQ(found->Source, CookDiagnosticSource::NavLink);
    EXPECT_EQ(found->SourceId, 0xabcdefu);
}

TEST_F(NavigationDocumentCookTest, NoSettingsCooksNoNavigation)
{
    const DocumentCookResult result = Cook(AuthorLevel(Vec3d(15.0f, 0.0f, 5.0f)));
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_FALSE(fs::exists(NavigationPath()));
    ASSERT_EQ(result.Diagnostics.size(), 1u);
    EXPECT_EQ(result.Diagnostics[0].Rule, "nav.settings.missing");
}

// An unresolved link identity survives the authored scene -- the editor must be
// able to load it to repair it -- and the cook is where it becomes an error.
TEST_F(NavigationDocumentCookTest, InvalidLinkIdentityRoundTripsAndFailsTheCook)
{
    WriteSettings();
    const DocumentCookResult result =
        Cook(AuthorLevel(Vec3d(15.0f, 0.0f, 5.0f), NavLinkId{}));
    EXPECT_FALSE(result.Success);
    EXPECT_TRUE(std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(),
        [](const CookDiagnostic& d) { return d.Rule == "nav.link.id_invalid"; }));
}
