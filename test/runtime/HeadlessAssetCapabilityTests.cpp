#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// What a process without graphics services can hold, and what it does with
// content describing what it cannot.
//
// A dedicated host loads the same content through the same front door as a
// windowed client. It simply has no cache that can hold a mesh or a texture,
// and that absence is a fact about how the process was composed -- fixed by
// which constructor ran -- rather than a failure to be reported.
//=============================================================================

namespace
{
    std::filesystem::path TempPath(std::string_view prefix, std::string_view extension)
    {
        static int counter = 0;
        std::string caseName = "unknown";
        if (const auto* info = testing::UnitTest::GetInstance()->current_test_info())
            caseName = std::string(info->test_suite_name()) + "_" + info->name();
        const std::string name = std::string(prefix) + "_" + caseName + "_"
            + std::to_string(++counter) + std::string(extension);
        return std::filesystem::temp_directory_path() / name;
    }

    // A real file plus its registry record: enough for the load and preload
    // paths to treat it as content that exists.
    class TempAsset
    {
    public:
        TempAsset(AssetRegistry& registry, AssetType type,
                  std::string_view extension, std::string_view contents)
        {
            File = TempPath("sencha_headless_asset", extension);
            std::ofstream out(File, std::ios::trunc);
            out << contents;
            out.close();

            AssetRecord record;
            record.Path = "asset://" + File.filename().generic_string();
            record.FilePath = File.generic_string();
            record.Type = type;
            record.SourceKind = AssetSourceKind::File;
            registry.Register(record);
            VirtualPath = record.Path;
        }

        ~TempAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        TempAsset(const TempAsset&) = delete;
        TempAsset& operator=(const TempAsset&) = delete;

        [[nodiscard]] const std::string& Path() const { return VirtualPath; }

    private:
        std::filesystem::path File;
        std::string VirtualPath;
    };
}

// The capability contract this whole design rests on: which kinds a process can
// hold is decided by the constructor and never changes, so a consumer may read
// a false answer as "not built with this" rather than "something broke".
TEST(HeadlessAssetCapability, HeadlessCompositionHoldsEveryKindButTheGpuOnes)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers);

    EXPECT_FALSE(assets.Assets.HasStore(AssetType::StaticMesh));
    EXPECT_FALSE(assets.Assets.HasStore(AssetType::Texture));
    EXPECT_FALSE(assets.Assets.HasStore(AssetType::SkinnedMesh));

    // Everything a server simulates from still loads.
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::Material));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::Audio));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::Skeleton));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::AnimationClip));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::Data));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::Scene));

    EXPECT_EQ(assets.StaticMeshes, nullptr);
    EXPECT_EQ(assets.Textures, nullptr);
    EXPECT_EQ(assets.SkinnedMeshes, nullptr);
}

// An unsupported kind resolves to an invalid handle rather than an error: the
// caller asked for something this process cannot hold, and the answer is "no
// asset", not "load failed".
TEST(HeadlessAssetCapability, LoadingAnUnsupportedKindYieldsNoHandle)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers);

    const TempAsset mesh(assets.Registry, AssetType::StaticMesh, ".smesh", "not read");
    const TempAsset texture(assets.Registry, AssetType::Texture, ".stex", "not read");

    EXPECT_FALSE(assets.Assets.LoadStaticMesh(mesh.Path()).IsValid());
    EXPECT_FALSE(assets.Assets.LoadTexture(texture.Path()).IsValid());
}

// A material references textures it cannot hold and still commits: the loader
// leaves the texture at its neutral default, which is what lets a dedicated
// host read the same material a client renders.
TEST(HeadlessAssetCapability, MaterialsStillLoadWithoutATextureCache)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers);

    const TempAsset material(assets.Registry, AssetType::Material, ".smat",
                             R"({"version": 2})");

    EXPECT_TRUE(assets.Assets.LoadMaterial(material.Path()).IsValid());
}

// A manifest describes content, not the process reading it. Staging bytes that
// can never commit would spend a server's IO to throw the result away, and
// would report a failure for content that is simply not its concern.
TEST(HeadlessAssetCapability, PreloadSkipsKindsTheProcessCannotHold)
{
    LoggingProvider logging;
    AsyncTaskQueue tasks(0);
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers);

    const TempAsset mesh(assets.Registry, AssetType::StaticMesh, ".smesh", "not read");
    const TempAsset material(assets.Registry, AssetType::Material, ".smat",
                             R"({"version": 2})");

    AssetPreloader preloader(logging, assets.Registry, assets.Assets, tasks);
    const std::vector<std::string> paths{ mesh.Path(), material.Path() };
    std::shared_ptr<AssetPreload> preload = preloader.Begin(paths);
    ASSERT_NE(preload, nullptr);

    (void)tasks.PumpWork();
    (void)tasks.DrainCompletions();
    EXPECT_EQ(preload->FailureCount(), 0u)
        << "a kind this process cannot hold is not a preload failure";
}
