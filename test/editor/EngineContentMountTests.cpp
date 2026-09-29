// An editor that resolves content the way the runtime does mounts the
// engine's own root after the project's, so the engine fact schema a rig
// extends resolves there too.

#include "project/ProjectContentMount.h"

#include <app/EngineContentRoot.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

TEST(EngineContentMount, TheEngineFactSchemaResolvesInAnEditorsStack)
{
    // A copy of the engine's fact schema in a root of its own, named by the
    // override, so the mount's import pass never writes into the source tree.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "sencha_engine_content_mount";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "animation");
    std::filesystem::copy_file(std::filesystem::path(SENCHA_REPO_ROOT) / "engine/assets/animation/engine.facts.sdata",
                               root / "animation/engine.facts.sdata");
    ASSERT_EQ(setenv("SENCHA_ENGINE_CONTENT", root.c_str(), 1), 0);
    EXPECT_EQ(EngineContentRoot(), root);

    {
        LoggingProvider logging;
        ComponentSerializerRegistry serializers;
        RuntimeAssets assets(logging, serializers);
        EXPECT_FALSE(assets.Registry.Contains("asset://animation/engine.facts.sdata"));
        MountEngineContent(assets, logging);
        EXPECT_TRUE(assets.Registry.Contains("asset://animation/engine.facts.sdata"));
        EXPECT_TRUE(assets.Assets.LoadLease("asset://animation/engine.facts.sdata", AssetType::Data));
    }

    unsetenv("SENCHA_ENGINE_CONTENT");
    std::filesystem::remove_all(root);
}
