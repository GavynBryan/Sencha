#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>

#include <cstdlib>

// Imports every source asset under a content root, the way a project mount
// does. Opt-in through the environment, like the other generators here: it is
// how a harness that needs cooked source assets gets them without standing up
// an editor.
//
// The golden images need this because the assetless level cook deliberately
// does not import: it cooks a level and nothing else, so a document, a font or
// a texture living beside that level never reaches the runtime otherwise.
TEST(ContentImport, Generate)
{
    const char* root = std::getenv("SENCHA_CONTENT_IMPORT_ROOT");
    if (root == nullptr || root[0] == '\0')
        GTEST_SKIP() << "set SENCHA_CONTENT_IMPORT_ROOT to import a content root";

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ContentImporterSet importers;

    ImportOnDemandStats stats{};
    const bool ok = ImportAssetsOnDemand(root, importers.Registry(), registry, logging, &stats);

    EXPECT_TRUE(ok) << "import reported a failure over '" << root << "'";
    EXPECT_EQ(stats.Failed, 0u);
}

#endif // SENCHA_ENABLE_COOK
