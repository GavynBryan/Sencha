#include "TextureImportProperties.h"

#include <gtest/gtest.h>

// A texture's import settings as properties: a change is applied at once, what
// the cook holds afterwards is what the card says, and a failed apply keeps the
// settings that were in force and says why.
namespace
{
const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* child = Find(node.Children, key))
            return child;
    }
    return nullptr;
}

PropertyEditResult Commit(TextureImportProperties& import, std::string_view key, std::string_view value)
{
    return import.BeginEdit(key) ? import.CommitEdit(key, value) : PropertyEditResult::Refused;
}

TextureImportProperties::Shown Rock()
{
    return { .Texture = "asset://textures/rock.png", .Settings = TextureImportSettings{}, .Cooked = "BC7 sRGB, 11 mips" };
}
} // namespace

TEST(TextureImportProperties, AChangeIsAppliedAndTheCookIsReadBack)
{
    std::vector<TextureImportSettings> applied;
    TextureImportProperties import(
        [&](const std::string&, const TextureImportSettings& settings, std::string& cooked, std::string&) {
            applied.push_back(settings);
            cooked = "RGBA8 sRGB, nearest, 1 mip";
            return true;
        },
        {});
    import.Show(Rock());
    EXPECT_EQ(Commit(import, "filter", "Nearest"), PropertyEditResult::Applied);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0].Filter, TextureFilter::Nearest);
    EXPECT_EQ(Commit(import, "filter", "Nearest"), PropertyEditResult::Unchanged) << "no cook for no change";
    EXPECT_EQ(Find(import.Nodes(), "cooked")->Value, "RGBA8 sRGB, nearest, 1 mip");
}

TEST(TextureImportProperties, AFailedApplyKeepsTheSettingsInForceAndSaysWhy)
{
    TextureImportProperties import(
        [](const std::string&, const TextureImportSettings&, std::string&, std::string& error) {
            error = "sidecar is read-only";
            return false;
        },
        {});
    import.Show(Rock());
    EXPECT_EQ(Commit(import, "compress", "false"), PropertyEditResult::Refused);
    EXPECT_EQ(Find(import.Nodes(), "compress")->Value, "true");
    EXPECT_EQ(Find(import.Nodes(), "problem")->Value, "sidecar is read-only");
}

TEST(TextureImportProperties, ATextureWithNoSourceOffersNothingToSetButCanStillMakeAMaterial)
{
    std::string made;
    TextureImportProperties import({}, [&](const std::string& texture) { made = texture; });
    import.Show({ .Texture = "asset://textures/cooked_only.png", .Settings = std::nullopt, .Cooked = "no source file" });
    EXPECT_EQ(Find(import.Nodes(), "usage"), nullptr);
    EXPECT_FALSE(import.BeginEdit("usage"));
    EXPECT_EQ(import.Act("import", "create_material", {}), PropertyEditResult::Applied);
    EXPECT_EQ(made, "asset://textures/cooked_only.png");
}
