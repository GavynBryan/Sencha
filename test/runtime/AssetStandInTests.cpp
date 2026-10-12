#include <anim/AnimationClipCache.h>
#include <assets/animation/AnimationClipAssetLoader.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/AssetStandIns.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetReferenceStore.h>
#include <core/assets/AssetRefCodec.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <jobs/AsyncTaskQueue.h>
#include <render/MaterialCache.h>
#include <render/TextureHandle.h>
#include <world/serialization/SceneSerializationContext.h>

#include "HandleFieldIo.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// A reference to an asset that cannot be loaded resolves to a stand-in under
// the same path: drawn conspicuously, saved back as the reference it stands
// for, reported as a load failure, and replaced in place once the asset loads.

namespace
{
class TempContent
{
public:
    TempContent()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path() / ("sencha_stand_in_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~TempContent()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    TempContent(const TempContent&) = delete;
    TempContent& operator=(const TempContent&) = delete;

    std::filesystem::path Write(std::string_view name, std::string_view text) const
    {
        const std::filesystem::path file = Root / name;
        std::ofstream(file, std::ios::trunc) << text;
        return file;
    }

private:
    std::filesystem::path Root;
};

constexpr std::string_view kRed = R"({"version": 2, "base_color_factor": [1.0, 0.0, 0.0, 1.0]})";
constexpr std::string_view kMalformed = R"({"version": 2, "unknown_key": true})";

struct StandInHarness
{
    StandInHarness()
    {
        RegisterAssetKind(Assets, AssetType::Material, Loader, &Materials, nullptr,
                          [this](std::string_view path) { return PutStandInMaterial(Materials, nullptr, path); });
    }

    void RegisterMaterial(const std::string& path, const std::filesystem::path& file)
    {
        ASSERT_TRUE(Registry.Register(AssetRecord{ .Type = AssetType::Material,
                                                   .SourceKind = AssetSourceKind::File,
                                                   .Path = path,
                                                   .FilePath = file.generic_string() }));
    }

    const Material* MaterialOf(const AssetLease& lease) const
    {
        return Materials.Get(MaterialHandle::FromToken(lease.OpaqueToken()));
    }

    bool HasFailure(std::string_view path, AssetType type) const
    {
        for (const AssetSystem::LoadFailure& failure : Assets.LoadFailures())
            if (failure.Path == path && failure.Type == type)
                return true;
        return false;
    }

    bool Reload(const std::string& path)
    {
        const AssetRecord* record = Registry.FindByPath(path);
        return record != nullptr && Assets.Reload(Loader.LoadStaged(*record, Assets.DefaultSource(), AssetPathsById{}));
    }

    // Stages every retry on the task lane and commits it at the drain.
    void RetryStandIns()
    {
        Assets.RetryStandIns(Tasks);
        (void)Tasks.PumpWork();
        (void)Tasks.DrainCompletions();
    }

    LoggingProvider Logging;
    AssetRegistry Registry{ Logging };
    AsyncTaskQueue Tasks{ 0 };
    MaterialCache Materials;
    MaterialAssetLoader Loader{ Logging, &Materials, nullptr };
    AssetSystem Assets{ Logging, Registry };
};

bool IsStandIn(const Material* material)
{
    return material != nullptr && material->Shading == MaterialShading::Unlit && material->DoubleSided;
}
} // namespace

TEST(AssetStandIn, TheStandInIsAnUnlitTwoSidedPinkAndBlackChecker)
{
    const TextureData texture = StandInTexture();
    ASSERT_TRUE(texture.IsValid());
    EXPECT_EQ(texture.Filter, TextureFilter::Nearest) << "a filtered checker of four texels is a pink smear";
    ASSERT_EQ(texture.Blob.size(), 16u);
    EXPECT_EQ(texture.Blob[0], 255);
    EXPECT_EQ(texture.Blob[1], 0);
    EXPECT_EQ(texture.Blob[2], 255);
    EXPECT_EQ(texture.Blob[4], 0) << "neighbouring texels are the same colour";

    const Material checkered = StandInMaterial(7);
    EXPECT_EQ(checkered.Shading, MaterialShading::Unlit) << "a stand-in in the dark is still invisible";
    EXPECT_TRUE(checkered.DoubleSided);
    EXPECT_EQ(checkered.BaseColorTextureIndex, 7u);

    const Material flat = StandInMaterial(UINT32_MAX);
    EXPECT_EQ(flat.BaseColor.X, 1.0f);
    EXPECT_EQ(flat.BaseColor.Y, 0.0f) << "without a checker it is still pink rather than white";
}

TEST(AssetStandIn, AMissingMaterialResolvesToAStandInUnderItsPath)
{
    StandInHarness h;
    const std::string path = "asset://materials/missing.smat";

    const AssetLease lease = h.Assets.LoadLease(path, AssetType::Material);
    ASSERT_TRUE(lease.IsValid()) << "a missing material vanished instead of standing in";
    EXPECT_TRUE(IsStandIn(h.MaterialOf(lease)));
    EXPECT_EQ(h.Assets.GetPathForLease(AssetType::Material, lease.OpaqueToken()), path)
        << "the stand-in lost the reference it stands for";
    EXPECT_TRUE(h.HasFailure(path, AssetType::Material)) << "nothing says why it stands in";
}

TEST(AssetStandIn, AskingAgainIsTheSameStandInAndNoNewFailure)
{
    StandInHarness h;
    const std::string path = "asset://materials/missing.smat";
    const AssetLease first = h.Assets.LoadLease(path, AssetType::Material);
    const std::uint64_t version = h.Assets.LoadFailuresVersion();

    const AssetLease second = h.Assets.LoadLease(path, AssetType::Material);
    EXPECT_EQ(first.OpaqueToken(), second.OpaqueToken());
    EXPECT_EQ(h.Assets.LoadFailuresVersion(), version);
    EXPECT_EQ(h.Assets.LoadFailures().size(), 1u);
}

TEST(AssetStandIn, AKindWithoutAStandInStillFails)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    MaterialAssetLoader loader(logging, &materials, nullptr);
    AssetSystem assets(logging, registry);
    RegisterAssetKind(assets, AssetType::Material, loader, &materials);

    EXPECT_FALSE(assets.LoadLease("asset://materials/missing.smat", AssetType::Material).IsValid());
    ASSERT_EQ(assets.LoadFailures().size(), 1u);
    EXPECT_EQ(assets.LoadFailures()[0].Type, AssetType::Material);
}

TEST(AssetStandIn, AMalformedMaterialStandsInUntilAReloadThatParsesReplacesIt)
{
    StandInHarness h;
    TempContent content;
    const std::string path = "asset://materials/broken.smat";
    h.RegisterMaterial(path, content.Write("broken.smat", kMalformed));

    const AssetLease held = h.Assets.LoadLease(path, AssetType::Material);
    ASSERT_TRUE(IsStandIn(h.MaterialOf(held)));
    ASSERT_TRUE(h.HasFailure(path, AssetType::Material));

    content.Write("broken.smat", kRed);
    ASSERT_TRUE(h.Reload(path));
    const Material* fixed = h.MaterialOf(held);
    ASSERT_NE(fixed, nullptr) << "the reload moved the material out from under its holder";
    EXPECT_EQ(fixed->Shading, MaterialShading::StandardLit);
    EXPECT_EQ(fixed->BaseColor.Y, 0.0f);
    EXPECT_FALSE(h.HasFailure(path, AssetType::Material)) << "the fixed material is still reported as failing";
}

TEST(AssetStandIn, AReloadThatFailsKeepsTheLastGoodVersionAndSaysWhy)
{
    StandInHarness h;
    TempContent content;
    const std::string path = "asset://materials/red.smat";
    h.RegisterMaterial(path, content.Write("red.smat", kRed));
    const AssetLease held = h.Assets.LoadLease(path, AssetType::Material);
    ASSERT_FALSE(IsStandIn(h.MaterialOf(held)));

    content.Write("red.smat", kMalformed);
    EXPECT_FALSE(h.Reload(path));
    EXPECT_FALSE(IsStandIn(h.MaterialOf(held))) << "a broken edit replaced a working material";
    EXPECT_TRUE(h.HasFailure(path, AssetType::Material));
}

TEST(AssetStandIn, AFileThatAppearsReplacesTheStandInWhereverItIsHeld)
{
    StandInHarness h;
    TempContent content;
    const std::string path = "asset://materials/later.smat";
    const AssetLease held = h.Assets.LoadLease(path, AssetType::Material);
    ASSERT_TRUE(IsStandIn(h.MaterialOf(held)));

    h.RegisterMaterial(path, content.Write("later.smat", kRed));
    h.RetryStandIns();

    const Material* arrived = h.MaterialOf(held);
    ASSERT_NE(arrived, nullptr);
    EXPECT_EQ(arrived->Shading, MaterialShading::StandardLit) << "the stand-in outlived the file it stood in for";
    EXPECT_FALSE(h.HasFailure(path, AssetType::Material));
}

TEST(AssetStandIn, ARetryStagesOnTheTaskLaneAndReplacesTheStandInAtTheDrain)
{
    StandInHarness h;
    TempContent content;
    const std::string path = "asset://materials/later.smat";
    const AssetLease held = h.Assets.LoadLease(path, AssetType::Material);
    h.RegisterMaterial(path, content.Write("later.smat", kRed));

    h.Assets.RetryStandIns(h.Tasks);
    EXPECT_TRUE(IsStandIn(h.MaterialOf(held))) << "the retry read and committed on the owner thread";

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    EXPECT_FALSE(IsStandIn(h.MaterialOf(held)));
    EXPECT_FALSE(h.HasFailure(path, AssetType::Material));
}

TEST(AssetStandIn, StandInsAreRetriedOnlyOnceTheRegistryChanges)
{
    StandInHarness h;
    TempContent content;
    const std::string path = "asset://materials/broken.smat";
    h.RegisterMaterial(path, content.Write("broken.smat", kMalformed));
    const AssetLease held = h.Assets.LoadLease(path, AssetType::Material);
    h.RetryStandIns();
    ASSERT_TRUE(IsStandIn(h.MaterialOf(held)));

    // Fixed on disk with nothing registered or rebound: the frame's retry
    // costs a comparison and reads nothing. The hot reload of the file is
    // what replaces it in that case.
    content.Write("broken.smat", kRed);
    h.RetryStandIns();
    EXPECT_TRUE(IsStandIn(h.MaterialOf(held)));

    h.RegisterMaterial("asset://materials/other.smat", content.Write("other.smat", kRed));
    h.RetryStandIns();
    EXPECT_FALSE(IsStandIn(h.MaterialOf(held)));
}

TEST(AssetStandIn, AStandInSavesAsTheReferenceItStandsFor)
{
    StandInHarness h;
    SceneSerializationContext context(h.Logging, &h.Assets);
    const std::string path = "asset://materials/missing.smat";

    const std::optional<JsonValue> stored = JsonParse("\"" + path + "\"");
    ASSERT_TRUE(stored.has_value());
    JsonReadArchive reading(*stored);
    MaterialHandle loaded;
    ASSERT_TRUE(LoadHandleField<MaterialHandle>(AssetType::Material, reading, "", loaded, context))
        << "a missing material failed the whole field, and with it the scene";
    ASSERT_TRUE(loaded.IsValid());

    JsonWriteArchive writing;
    ASSERT_TRUE(SaveHandleField<MaterialHandle>(AssetType::Material, writing, "material", loaded, context));
    const std::optional<AssetRef> written = AssetRefFromJson(writing.TakeValue());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->Path, path) << "saving through a stand-in lost the reference";
    h.Materials.Release(loaded);
}

TEST(AssetStandIn, AnAsyncLoadDeliversAMissingDependencyAsItsStandIn)
{
    StandInHarness h;
    using TextureReferences = AssetReferenceStore<TextureHandle, AssetType::Texture>;
    TextureReferences textures;
    AssetKindRegistration kind = MakeBuiltinAssetKind(AssetType::Texture);
    kind.Stager = &textures;
    kind.Store = &textures;
    kind.Commit = [&textures](AssetStaging&& staged) {
        const TextureHandle handle = textures.CommitTyped(std::move(staged));
        return handle.IsValid() ? AssetLease::Adopt(AssetType::Texture, textures, handle.ToToken()) : AssetLease{};
    };
    kind.Reload = [](AssetStaging&&) { return true; };
    kind.StandIn = [&textures](std::string_view path) {
        return AssetLease::Adopt(AssetType::Texture, textures, textures.Intern(path).ToToken());
    };
    ASSERT_TRUE(h.Assets.Kinds().Register(std::move(kind)));

    TempContent content;
    const std::string material = "asset://materials/painted.smat";
    const std::string texture = "asset://textures/missing.png";
    h.RegisterMaterial(material, content.Write("painted.smat", R"({"version": 2, "base_color_texture": ")" + texture
                                                                     + R"("})"));

    AsyncTaskQueue tasks{ 0 };
    AssetPreloader preloader(h.Logging, h.Registry, h.Assets, tasks);
    const std::vector<std::string> paths{ material };
    const auto preload = preloader.Begin(paths);
    (void)tasks.PumpWork();
    (void)tasks.DrainCompletions();

    ASSERT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u) << "a missing texture refused the material that names it";
    EXPECT_TRUE(h.Materials.Find(material).IsValid());
    EXPECT_TRUE(h.HasFailure(texture, AssetType::Texture));
}

template <typename TLoader, typename TCache>
concept TakesAStandIn = requires(AssetSystem& assets, TLoader& loader, TCache* cache,
                                 std::function<AssetLease(std::string_view)> standIn) {
    RegisterAssetKind(assets, AssetType::Material, loader, cache, nullptr, standIn);
};

static_assert(TakesAStandIn<MaterialAssetLoader, MaterialCache>);
static_assert(!TakesAStandIn<AnimationClipAssetLoader, AnimationClipCache>,
              "a stand-in for a kind that cannot reload would never be replaced");

TEST(AssetStandIn, AStandInNeedsAReloadToBeReplacedBy)
{
    MaterialCache materials;
    AssetKindRegistry kinds;
    AssetKindRegistration kind = MakeBuiltinAssetKind(AssetType::Material);
    kind.Store = &materials;
    kind.Commit = [](AssetStaging&&) { return AssetLease{}; };
    kind.StandIn = [](std::string_view) { return AssetLease{}; };
    EXPECT_FALSE(kinds.Register(std::move(kind))) << "a stand-in nothing could ever replace was accepted";
}

TEST(AssetRegistryRevision, MovesOnlyWhenARecordIsAddedDroppedOrRebound)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    const AssetRecord record{ .Type = AssetType::Material,
                              .SourceKind = AssetSourceKind::File,
                              .Path = "asset://materials/a.smat",
                              .FilePath = "a.smat" };

    std::uint64_t revision = registry.Revision();
    ASSERT_TRUE(registry.Register(record));
    EXPECT_NE(registry.Revision(), revision);

    revision = registry.Revision();
    EXPECT_FALSE(registry.Register(record));
    EXPECT_TRUE(registry.RegisterOrVerify(record));
    EXPECT_EQ(registry.Revision(), revision) << "a registration that changed nothing moved the revision";

    ASSERT_TRUE(registry.AssignId(record.Path, AssetId{ 0x1234 }));
    EXPECT_NE(registry.Revision(), revision);

    revision = registry.Revision();
    ASSERT_TRUE(registry.Unregister(record.Path));
    EXPECT_NE(registry.Revision(), revision);
}
