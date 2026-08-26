// Utility generator: cooks one authored level into a chosen assets root, the
// headless half of the live GPU validation workflow (cook here, then view the
// output in SceneViewer or diff the atlas offline). Complements the scene
// generators: this one takes existing authored content instead of building it.
//
// Skipped unless SENCHA_COOK_LEVEL (authored .json path) and SENCHA_COOK_ROOT
// (assets root to cook into) are both set.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"

#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

TEST(CookLevel, Generate)
{
    const char* level = std::getenv("SENCHA_COOK_LEVEL");
    const char* root = std::getenv("SENCHA_COOK_ROOT");
    if (level == nullptr || root == nullptr)
        GTEST_SKIP() << "set SENCHA_COOK_LEVEL and SENCHA_COOK_ROOT to cook a level";

    RegisterDocumentSerializers();
    // With a console sink, so a load or cook failure names its reason instead
    // of surfacing as a bare 'could not load'.
    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();
    const DocumentCookResult result = CookDocument(
        std::filesystem::path(level), std::filesystem::path(root), /*cellSize*/ 16.0,
        &logging);
    ASSERT_TRUE(result.Success) << result.Error;
    std::printf("cooked '%s': cells=%zu directLights=%zu atlas=%ux%u probes=%zu\n",
                level, result.CellCount, result.DirectLightCount,
                result.LightmapAtlasWidth, result.LightmapAtlasHeight,
                result.ProbeCount);
}
