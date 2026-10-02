#pragma once

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <filesystem>

// The engine's content and the fixture project's, with their cooked caches,
// scanned the way a project opens: the example rigs under
// asset://animation/examples/ load through the runtime asset pipeline.
struct AnimationExampleContent
{
    std::filesystem::path Repo{ SENCHA_REPO_ROOT };
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };

    AnimationExampleContent()
    {
        for (const std::filesystem::path& root : { Repo / "engine/assets", Repo / "engine/assets/.cooked",
                                                  Repo / "test/fixtures/content/assets",
                                                  Repo / "test/fixtures/content/assets/.cooked" })
            (void)ScanAssetsDirectory(root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
    }
};
