#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

//=============================================================================
// Mounting a content root: the two steps a runtime host runs back to back and
// the editor runs with an import between them. The fixture is a root with one
// authored asset, one cooked artifact the physical scan can key by location,
// one the index alone can key (a cooked texture still serving its source
// virtual path), and an id map naming the authored one.
//=============================================================================
namespace
{
    constexpr std::string_view kAuthoredPath = "asset://materials/dev.smat";
    constexpr std::string_view kCookedScannedPath = "asset://meshes/box.smesh";
    constexpr std::string_view kIndexOnlyPath = "asset://textures/wall.png";

    void WriteFile(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << text;
    }

    class ContentRoot
    {
    public:
        explicit ContentRoot(std::string_view name)
        {
            Root = std::filesystem::temp_directory_path()
                   / ("sencha-content-mount-" + std::string(name));
            std::filesystem::remove_all(Root);

            WriteFile(Root / "materials" / "dev.smat", "{}");
            // Inside the cook cache, where the authored scan never descends and
            // the cooked scan keys it by its location under that root.
            WriteFile(Root / ".cooked" / "meshes" / "box.smesh", "");
            WriteFile(Root / ".cooked" / "textures" / "wall.stex", "");
            WriteFile(Root / ".cooked" / "index.json", R"({
              "sources": [
                { "artifacts": [
                  { "path": "asset://textures/wall.png",
                    "file": ".cooked/textures/wall.stex",
                    "type": "Texture" }
                ] }
              ]
            })");

            AssetIdMap ids;
            AuthoredId = ids.EnsureId(kAuthoredPath, 0);
            EXPECT_TRUE(ids.SaveToFile((Root / kAssetIdMapFileName).string()));
        }

        ~ContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        [[nodiscard]] ContentRootPaths Paths() const { return ResolveContentRoot(Root); }
        [[nodiscard]] AssetId Id() const { return AuthoredId; }

    private:
        std::filesystem::path Root;
        AssetId AuthoredId;
    };

    // A stack composed the way a dedicated host composes one: no graphics, so
    // no cache holds a mesh, but every kind is still classified and registered.
    struct HeadlessStack
    {
        HeadlessStack()
            : Assets(Logging, Serializers)
        {
            RegisterEngineSceneSerializers(Serializers);
        }

        [[nodiscard]] Logger& Log() { return Logging.GetLogger<HeadlessStack>(); }

        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets;
    };
}

TEST(ContentMount, ResolveNamesTheCookCacheBesideTheAuthoredRoot)
{
    const ContentRootPaths paths = ResolveContentRoot("content");
    EXPECT_EQ(paths.Authored, std::filesystem::path("content"));
    EXPECT_EQ(paths.Cooked, std::filesystem::path("content") / ".cooked");
}

// The scan alone registers what physical layout can key, and nothing else. The
// separation is what lets the editor cook on demand before the index is read.
TEST(ContentMount, ScanRegistersAuthoredAndCookedFilesButNotTheIndex)
{
    const ContentRoot root("scan");
    HeadlessStack stack;

    ScanContentRoot(root.Paths(), stack.Assets);

    EXPECT_TRUE(stack.Assets.Registry.Contains(std::string(kAuthoredPath)));
    EXPECT_TRUE(stack.Assets.Registry.Contains(std::string(kCookedScannedPath)));
    EXPECT_FALSE(stack.Assets.Registry.Contains(std::string(kIndexOnlyPath)));

    // No id map has been applied, so the authored record resolves by path only.
    const AssetRecord* record =
        stack.Assets.Registry.FindByPath(std::string(kAuthoredPath));
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->Id.IsValid());
}

TEST(ContentMount, CookedRegistrationAddsTheIndexEntriesAndStampsIds)
{
    const ContentRoot root("cooked");
    HeadlessStack stack;

    ScanContentRoot(root.Paths(), stack.Assets);
    RegisterCookedContent(root.Paths(), stack.Assets, stack.Log());

    EXPECT_TRUE(stack.Assets.Registry.Contains(std::string(kIndexOnlyPath)));

    const AssetRecord* record =
        stack.Assets.Registry.FindByPath(std::string(kAuthoredPath));
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Id, root.Id());
}

// The editor runs the two steps with an import between them; a runtime host
// runs them back to back. With nothing to import they must agree, or the
// viewport the editor calls WYSIWYG is resolving against a different registry
// than the runtime does.
TEST(ContentMount, MountEqualsScanThenRegister)
{
    const ContentRoot root("equal");
    HeadlessStack stepwise;
    HeadlessStack mounted;

    ScanContentRoot(root.Paths(), stepwise.Assets);
    RegisterCookedContent(root.Paths(), stepwise.Assets, stepwise.Log());
    MountContentRoot(root.Paths(), mounted.Assets, mounted.Log());

    ASSERT_EQ(stepwise.Assets.Registry.Records().size(),
              mounted.Assets.Registry.Records().size());
    for (const auto& [path, record] : stepwise.Assets.Registry.Records())
    {
        const AssetRecord* other = mounted.Assets.Registry.FindByPath(path);
        ASSERT_NE(other, nullptr) << path;
        EXPECT_EQ(other->Type, record.Type) << path;
        EXPECT_EQ(other->Id, record.Id) << path;
        EXPECT_EQ(other->FilePath, record.FilePath) << path;
    }
}

// A root that has never been cooked has no id map. That is the first-run state
// of a new project, not a failure: everything still resolves by path.
TEST(ContentMount, AMissingIdMapLeavesThePathsResolvable)
{
    const ContentRoot root("nomap");
    HeadlessStack stack;
    std::filesystem::remove(root.Paths().Authored / kAssetIdMapFileName);

    MountContentRoot(root.Paths(), stack.Assets, stack.Log());

    const AssetRecord* record =
        stack.Assets.Registry.FindByPath(std::string(kAuthoredPath));
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->Id.IsValid());
}
