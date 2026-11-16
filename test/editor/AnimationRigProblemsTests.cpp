#include "authoring/AnimationRigProblems.h"
#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "documents/DocumentSourceSet.h"

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

namespace
{
constexpr std::string_view kBase = "asset://animation/base.facts.sdata";
constexpr std::string_view kGame = "asset://animation/game.facts.sdata";

const PropertyNode* Within(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* child = Within(node.Children, key))
            return child;
    }
    return nullptr;
}

AnimDiagnostic Unprovided(std::string_view fact)
{
    return { .Severity = AnimDiagnosticSeverity::Warning,
             .Code = "anim.fact.unprovided",
             .AssetPath = std::string(kGame),
             .FieldPath = std::format("$.data.slots{{{}}}.name", fact),
             .Message = std::format("Nothing in this World provides '{}'.", fact) };
}
} // namespace

TEST(AnimationRigProblems, AProblemOnAnInheritedFactNamesWhereItCameFromAndItsRowReadsAsInherited)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sencha_rig_problems_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "animation");
    std::ofstream(root / "animation" / "base.facts.sdata") << R"({ "type": "animation.fact_schema", "version": 2,
        "abstract": true, "data": { "slots": [ { "name": "Dead", "kind": "bool" } ] } })";
    std::ofstream(root / "animation" / "game.facts.sdata") << R"({ "type": "animation.fact_schema", "version": 2,
        "extends": "asset://animation/base.facts.sdata",
        "data": { "slots": [ { "name": "Crouched", "kind": "bool" } ] } })";
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    DocumentSourceSet sources;
    DataDocumentStore store(assets, sources);
    (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());

    const std::vector<AnimDiagnostic> diagnostics{ Unprovided("Dead"), Unprovided("Crouched") };
    const std::vector<Problem> problems = AnimationRigProblems(diagnostics, {}, store);
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_NE(problems[0].Message.find("(inherited from 'asset://animation/base.facts.sdata')"), std::string::npos)
        << problems[0].Message;
    EXPECT_EQ(problems[0].Location, "$.data.slots{Dead}.name") << "it points at the effective row";
    ASSERT_TRUE(problems[0].Document.has_value());
    EXPECT_EQ(problems[0].Document->Key, kGame);
    EXPECT_EQ(problems[1].Message.find("inherited"), std::string::npos) << "the schema's own fact is its own";

    {
        DataDocumentSet set(store, DataDocumentSetConfig{ .ContentRoot = root,
                                                          .Subtypes = { std::string(kAnimFactSchemaType) } });
        std::string error;
        DataDocument* game = set.OpenOrFocus(kGame, error);
        ASSERT_NE(game, nullptr) << error;
        DataProperties properties(set, [game] { return game; });
        const std::vector<PropertyNode> nodes = properties.Nodes();
        const PropertyNode* dead = Within(nodes, problems[0].Location);
        ASSERT_NE(dead, nullptr) << "the problem's location is a row of the schema as it composes";
        EXPECT_NE(dead->Descriptor.Tooltip.find("inherited from asset://animation/base.facts.sdata"), std::string::npos);
        EXPECT_EQ(dead->Descriptor.Tooltip.find("set here"), std::string::npos);
        EXPECT_TRUE(std::ranges::none_of(dead->Descriptor.Verbs, [](const PropertyVerb& verb) { return verb.Id == "revert"; }));
        EXPECT_TRUE(game->Data()->Find("slots")->AsArray().size() == 1) << "nothing inherited was written here";
        sources.DiscardAll();
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
