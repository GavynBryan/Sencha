// A definition built from a fragment is authored in Kyusu as it composes, each
// row saying which definition its value came from.

#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "documents/DocumentSourceSet.h"

#include <abilities/CuePresentationData.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
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
} // namespace

TEST(CompositionGateAuthoring, ACuePresentationShowsWhatItsFragmentAddsAndWhatItSetsItself)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sencha_gate_authoring_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "cues");
    std::ofstream(root / "cues" / "muzzle.sdata") << R"({ "type": "cue.presentation", "version": 1, "abstract": true,
        "data": { "cues": [ { "key": "k_muzzle", "cue": "cue.muzzle", "sustain": [ { "lease": "test.flash" } ] } ] } })";
    std::ofstream(root / "cues" / "rifle.sdata") << R"({ "type": "cue.presentation", "version": 1,
        "includes": [ "asset://cues/muzzle.sdata" ],
        "data": { "cues": { "k_shot": { "cue": "cue.shot", "sustain": [ { "lease": "test.glow" } ] } } } })";
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    DocumentSourceSet sources;
    DataDocumentStore store(assets, sources);
    (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());
    {
        DataDocumentSet set(store, DataDocumentSetConfig{ .ContentRoot = root,
                                                          .Subtypes = { std::string(kCuePresentationType) } });
        std::string error;
        DataDocument* rifle = set.OpenOrFocus("asset://cues/rifle.sdata", error);
        ASSERT_NE(rifle, nullptr) << error;
        EXPECT_TRUE(rifle->IsSemanticallyValid()) << rifle->ValidationErrors().front().Message;

        DataProperties properties(set, [rifle] { return rifle; });
        const std::vector<PropertyNode> nodes = properties.Nodes();
        const PropertyNode* muzzle = Within(nodes, "$.data.cues{k_muzzle}.cue");
        ASSERT_NE(muzzle, nullptr) << "the fragment's cue is laid out by its key";
        EXPECT_NE(muzzle->Descriptor.Tooltip.find("from asset://cues/muzzle.sdata"), std::string::npos)
            << muzzle->Descriptor.Tooltip;
        EXPECT_EQ(muzzle->Descriptor.Tooltip.find("inherited"), std::string::npos) << "a fragment is not a base";
        const PropertyNode* shot = Within(nodes, "$.data.cues{k_shot}.cue");
        ASSERT_NE(shot, nullptr);
        EXPECT_NE(shot->Descriptor.Tooltip.find("set here"), std::string::npos) << shot->Descriptor.Tooltip;
        sources.DiscardAll();
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
