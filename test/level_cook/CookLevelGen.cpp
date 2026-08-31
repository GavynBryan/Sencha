// Utility generator: cooks authored levels into a chosen assets root, the
// headless half of the live GPU validation workflow (cook here, then view the
// output in SceneViewer or diff the atlas offline). Complements the scene
// generators: this one takes existing authored content instead of building it.
//
// Skipped unless SENCHA_COOK_LEVEL (authored .sscene paths, comma-separated)
// and SENCHA_COOK_ROOT (assets root to cook into) are both set. Registers the
// template game's components beside the document serializers, so authored
// template content (player_start, turret_mount, spin) cooks intact with no
// module load.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"

#include "TemplateComponents.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

TEST(CookLevel, Generate)
{
    const char* levels = std::getenv("SENCHA_COOK_LEVEL");
    const char* root = std::getenv("SENCHA_COOK_ROOT");
    if (levels == nullptr || root == nullptr)
        GTEST_SKIP() << "set SENCHA_COOK_LEVEL and SENCHA_COOK_ROOT to cook a level";

    RegisterDocumentSerializers();
    ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
    RegisterTemplateComponents(registrar);

    // With a console sink, so a load or cook failure names its reason instead
    // of surfacing as a bare 'could not load'.
    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();

    // The headless asset composition, so authored content that names a data
    // asset -- a movement profile, a game's own settings -- resolves rather
    // than being dropped on the way through. Mesh and texture references still
    // need the windowed composition: those caches hold GPU resources.
    RuntimeAssets assets(logging, EditorSceneSerializers());
    (void)ScanAssetsDirectory(root, assets.Registry, assets.Assets.Kinds());

    std::string_view remaining(levels);
    while (!remaining.empty())
    {
        const std::size_t comma = remaining.find(',');
        const std::string_view level = remaining.substr(0, comma);
        remaining = comma == std::string_view::npos
            ? std::string_view{}
            : remaining.substr(comma + 1);
        if (level.empty())
            continue;
        const DocumentCookResult result = CookDocument(
            std::filesystem::path(level), std::filesystem::path(root),
            /*cellSize*/ 16.0, &logging, &assets);
        ASSERT_TRUE(result.Success) << level << ": " << result.Error;
        std::printf(
            "cooked '%.*s': cells=%zu directLights=%zu atlas=%ux%u probes=%zu\n",
            static_cast<int>(level.size()), level.data(), result.CellCount,
            result.DirectLightCount, result.LightmapAtlasWidth,
            result.LightmapAtlasHeight, result.ProbeCount);
    }
}
