// Gameplay tags a project declares as content: every declaration asset's names
// are registered when content is published, in asset path order, and one that
// does not compile is reported rather than skipped.

#include <assets/runtime/ContentTagDeclarations.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    struct Content
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_content_tags";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        Content()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "b");
            std::filesystem::create_directories(Root / "a");
        }
        ~Content() { std::filesystem::remove_all(Root); }

        void Write(const std::string& relative, std::string_view text)
        {
            std::ofstream(Root / relative) << text;
        }

        void Scan() { (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds()); }
    };
}

TEST(ContentTagDeclarations, EveryDeclaredNameIsRegisteredInPathOrder)
{
    Content content;
    content.Write("b/walker.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "Anim.Walk", "Anim.Idle" ] } })");
    content.Write("a/door.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "Anim.Door.Open" ] } })");
    content.Scan();

    GameplayTagRegistry tags;
    std::vector<std::string> errors;
    DeclareContentTags(content.Assets, tags, errors);
    EXPECT_TRUE(errors.empty()) << errors.front();
    ASSERT_TRUE(tags.FindTag("Anim.Door.Open").IsValid());
    ASSERT_TRUE(tags.FindTag("Anim.Walk").IsValid());
    ASSERT_TRUE(tags.FindTag("Anim.Idle").IsValid());
    // a/ before b/, then list order: the same numbering on every machine.
    EXPECT_LT(tags.FindTag("Anim.Door.Open").Value, tags.FindTag("Anim.Walk").Value);
    EXPECT_LT(tags.FindTag("Anim.Walk").Value, tags.FindTag("Anim.Idle").Value);

    // Declaring again changes nothing.
    const GameplayTagId walk = tags.FindTag("Anim.Walk");
    DeclareContentTags(content.Assets, tags, errors);
    EXPECT_EQ(tags.FindTag("Anim.Walk"), walk);
}

TEST(ContentTagDeclarations, AMalformedDeclarationIsReported)
{
    Content content;
    content.Write("a/bad.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "not a tag" ] } })");
    content.Scan();
    GameplayTagRegistry tags;
    std::vector<std::string> errors;
    DeclareContentTags(content.Assets, tags, errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("bad.tags.sdata"), std::string::npos) << errors.front();
}
