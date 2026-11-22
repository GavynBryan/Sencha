#include "document/DocumentSerialization.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"

#include "ArenaComponents.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

TEST(CookWorld, Generate)
{
    const char* worldPath = std::getenv("SENCHA_COOK_WORLD");
    const char* root = std::getenv("SENCHA_COOK_ROOT");
    if (worldPath == nullptr || root == nullptr)
        GTEST_SKIP() << "set SENCHA_COOK_WORLD and SENCHA_COOK_ROOT to cook a world";

    RegisterDocumentSerializers();
    ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
    RegisterArenaComponents(registrar);

    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();
    RuntimeAssets assets(logging, EditorSceneSerializers(), RuntimeAssets::ReferenceOnly{});
    (void)ScanAssetsDirectory(root, assets.Registry, assets.Assets.Kinds());

    WorldDocument world(logging);
    ASSERT_TRUE(world.LoadWorld(worldPath)) << worldPath;
    const WorldCookResult cooked = CookWorld(world, std::filesystem::path(root), 16.0, logging, &assets);
    ASSERT_TRUE(cooked.Success) << cooked.Error;
}
