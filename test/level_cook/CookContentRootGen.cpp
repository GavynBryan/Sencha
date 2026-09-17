// Utility generator: cooks every source under an assets root into its .cooked
// sibling, using the same importer set an editor uses on demand.
//
// This is how engine-owned content gets its committed artifacts. A shipping
// game builds with SENCHA_ENABLE_COOK off and has no cooker in it at all, so
// anything the engine ships for a player to see -- the default pause menu and
// the face it draws with -- has to be cooked ahead of time and committed, the
// way editor/ui/.cooked already is. Re-run this after editing that content:
//
//   ctest --test-dir build -R CookEngineUiContent
//
// Skipped unless SENCHA_COOK_ROOT names the root to cook.

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <string>

TEST(CookContentRoot, Generate)
{
    const char* root = std::getenv("SENCHA_COOK_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "set SENCHA_COOK_ROOT to the assets root to cook";

    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();

    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers, RuntimeAssets::ReferenceOnly{});
    (void)ScanAssetsDirectory(root, assets.Registry, assets.Assets.Kinds());

    ContentImporterSet importers(nullptr);
    ImportOnDemandStats stats;
    const bool ok = ImportAssetsOnDemand(root, importers.Registry(), assets.Registry,
                                         logging, &stats);

    std::printf("cooked '%s': seen=%zu fresh=%zu imported=%zu failed=%zu\n",
                root, stats.SourcesSeen, stats.CookedFresh, stats.Imported, stats.Failed);
    EXPECT_EQ(stats.Failed, 0u);
    EXPECT_TRUE(ok);
    EXPECT_GT(stats.SourcesSeen, 0u) << "nothing under the root matched an importer";
}
