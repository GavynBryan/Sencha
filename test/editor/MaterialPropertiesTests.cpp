#include "MaterialDocumentSet.h"
#include "MaterialProperties.h"

#include "documents/DocumentSourceSet.h"

#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

// The open material's parameters as properties: grouped as the shading model
// reads them, a drag previewed on the material and made one step when it lets
// go, and a texture slot chosen from the registry's source textures.
namespace
{
class MaterialPropertiesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Dir = std::filesystem::temp_directory_path() / "sencha_material_properties_tests";
        std::filesystem::remove_all(Dir);
        std::filesystem::create_directories(Dir);
        const std::filesystem::path file = Dir / "rock.smat";
        std::ofstream(file, std::ios::trunc) << R"({"version": 2})";
        std::string error;
        Tab = Set.OpenOrFocus("asset://materials/rock.smat", file.string(), &error);
        ASSERT_NE(Tab, nullptr) << error;
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        std::filesystem::remove_all(Dir);
    }

    [[nodiscard]] bool Shows(std::string_view key) const
    {
        for (const PropertyNode& group : Properties.Nodes())
            for (const PropertyNode& node : group.Children)
                if (node.Key == key)
                    return true;
        return false;
    }

    std::filesystem::path Dir;
    DocumentSourceSet Sources;
    MaterialDocumentSet Set{ Sources, [](MaterialEditTab&, const MaterialDescription&) {} };
    LoggingProvider Logging;
    AssetRegistry Registry{ Logging };
    MaterialProperties Properties{ Set, Registry };
    MaterialEditTab* Tab = nullptr;
};
} // namespace

TEST_F(MaterialPropertiesTest, TheParametersShownAreTheOnesTheShadingReads)
{
    EXPECT_TRUE(Shows("roughness"));
    EXPECT_FALSE(Shows("alpha_cutoff")) << "an opaque material has no cutoff";
    ASSERT_TRUE(Properties.BeginEdit("shading"));
    EXPECT_EQ(Properties.CommitEdit("shading", "Unlit"), PropertyEditResult::Applied);
    EXPECT_FALSE(Shows("roughness")) << "an unlit material has no surface";
    ASSERT_TRUE(Properties.BeginEdit("alpha_mode"));
    EXPECT_EQ(Properties.CommitEdit("alpha_mode", "Mask"), PropertyEditResult::Applied);
    EXPECT_TRUE(Shows("alpha_cutoff"));
}

TEST_F(MaterialPropertiesTest, ADragPreviewsAndBecomesOneStepWhenItLetsGo)
{
    ASSERT_TRUE(Properties.BeginEdit("metallic"));
    EXPECT_EQ(Properties.PreviewEdit("metallic", "0.3"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(Tab->Session.Working().MetallicFactor, 0.3f) << "the material shows it at once";
    EXPECT_FALSE(Tab->Commands.CanUndo()) << "not a step yet";
    EXPECT_EQ(Properties.PreviewEdit("metallic", "0.6"), PropertyEditResult::Applied);
    EXPECT_EQ(Properties.CommitEdit("metallic", "0.7"), PropertyEditResult::Applied);
    ASSERT_TRUE(Tab->Commands.CanUndo());
    Tab->Commands.Undo();
    EXPECT_FLOAT_EQ(Tab->Session.Working().MetallicFactor, 0.0f) << "one step back to where the drag began";
}

TEST_F(MaterialPropertiesTest, ACancelledDragPutsTheMaterialBack)
{
    ASSERT_TRUE(Properties.BeginEdit("base_color"));
    EXPECT_EQ(Properties.PreviewEdit("base_color", "1, 0, 0"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(Tab->Session.Working().BaseColorFactor.Y, 0.0f);
    Properties.CancelEdit("base_color");
    EXPECT_FLOAT_EQ(Tab->Session.Working().BaseColorFactor.Y, 1.0f);
    EXPECT_FALSE(Tab->Commands.CanUndo());
    ASSERT_TRUE(Properties.BeginEdit("roughness"));
    EXPECT_EQ(Properties.CommitEdit("roughness", "not a number"), PropertyEditResult::Refused);
}

TEST_F(MaterialPropertiesTest, ATextureSlotIsChosenFromTheSourceTextures)
{
    ASSERT_TRUE(Registry.Register({ .Type = AssetType::Texture, .SourceKind = AssetSourceKind::File, .Path = "asset://textures/rock.png" }));
    ASSERT_TRUE(Registry.Register({ .Type = AssetType::Texture, .SourceKind = AssetSourceKind::File, .Path = "asset://textures/rock.png.stex" }));
    const std::vector<std::string> candidates = Properties.Candidates("base_color_texture");
    ASSERT_EQ(candidates.size(), 2u);
    EXPECT_EQ(candidates[0], "") << "none comes first";
    EXPECT_EQ(candidates[1], "asset://textures/rock.png") << "the cook is not offered";
    ASSERT_TRUE(Properties.BeginEdit("base_color_texture"));
    EXPECT_EQ(Properties.CommitEdit("base_color_texture", "asset://textures/rock.png"), PropertyEditResult::Applied);
    EXPECT_EQ(Tab->Session.Working().BaseColorTexture.Path, "asset://textures/rock.png");
}
