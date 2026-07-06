#include "ScriptTestSupport.h"

#include <assets/script/ScriptAssetLoader.h>
#include <assets/script/ScriptCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/logging/LoggingProvider.h>
#include <script/ScriptCompiler.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/ScriptCook.h>
#endif

namespace
{
    // A validated module built from source, wrapped for cache/loader tests
    // that don't need the cook layer.
    std::shared_ptr<const ScriptModule> BuildModule(std::string_view source)
    {
        ScriptCompileResult result = CompileScript("test.t", source, {});
        EXPECT_TRUE(result.Ok) << result.Error.Message;
        return std::make_shared<const ScriptModule>(std::move(result.Module));
    }

    constexpr std::string_view kDoor =
        "component DoorState {\n"
        "    open: bool = false\n"
        "    locked: bool = true\n"
        "}\n"
        "interaction OpenDoor {\n"
        "    fn interact(ctx: InteractionContext) {\n"
        "        ctx.target.DoorState.open = true\n"
        "    }\n"
        "}\n";
}

TEST(ScriptCache, RegisterFindAndDedup)
{
    LoggingProvider logging;
    ScriptCache cache(logging);

    std::shared_ptr<const ScriptModule> module = BuildModule(kDoor);
    const ScriptHandle a = cache.Register("scripts/door.t", module);
    ASSERT_TRUE(a.IsValid());
    EXPECT_EQ(cache.Get(a).get(), module.get());
    EXPECT_EQ(cache.GetName(a), "scripts/door.t");

    // Same path dedups to the same slot.
    const ScriptHandle b = cache.Register("scripts/door.t", BuildModule(kDoor));
    EXPECT_EQ(cache.Find("scripts/door.t").Index, a.Index);
    cache.Release(b);
    cache.Release(a);
}

TEST(ScriptCache, ReloadSameLayoutSwapsInPlace)
{
    LoggingProvider logging;
    ScriptCache cache(logging);

    std::shared_ptr<const ScriptModule> first = BuildModule(kDoor);
    const ScriptHandle handle = cache.Register("scripts/door.t", first);

    // A logic-only edit keeps the component schema, so it swaps in place and
    // the handle stays valid pointing at the new module.
    constexpr std::string_view edited =
        "component DoorState {\n"
        "    open: bool = false\n"
        "    locked: bool = true\n"
        "}\n"
        "interaction OpenDoor {\n"
        "    fn interact(ctx: InteractionContext) {\n"
        "        ctx.target.DoorState.open = true\n"
        "        ctx.target.DoorState.locked = true\n"
        "    }\n"
        "}\n";
    std::shared_ptr<const ScriptModule> second = BuildModule(edited);
    EXPECT_EQ(cache.ReloadInPlace("scripts/door.t", second), ScriptReloadResult::Swapped);
    EXPECT_EQ(cache.Get(handle).get(), second.get());
    cache.Release(handle);
}

TEST(ScriptCache, ReloadLayoutChangeIsRejected)
{
    LoggingProvider logging;
    ScriptCache cache(logging);
    const ScriptHandle handle = cache.Register("scripts/door.t", BuildModule(kDoor));

    // Adding a field changes the component schema hash: no in-place swap.
    constexpr std::string_view relaidOut =
        "component DoorState {\n"
        "    open: bool = false\n"
        "    locked: bool = true\n"
        "    heavy: bool = false\n"
        "}\n"
        "interaction OpenDoor {\n"
        "    fn interact(ctx: InteractionContext) {\n"
        "        ctx.target.DoorState.open = true\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(cache.ReloadInPlace("scripts/door.t", BuildModule(relaidOut)),
              ScriptReloadResult::LayoutChanged);
    cache.Release(handle);
}

TEST(ScriptCache, ReloadUnknownPathIsNotResident)
{
    LoggingProvider logging;
    ScriptCache cache(logging);
    EXPECT_EQ(cache.ReloadInPlace("scripts/absent.t", BuildModule(kDoor)),
              ScriptReloadResult::NotResident);
}

TEST(ScriptCache, SchemaHashIgnoresLogicButTracksLayout)
{
    // The hash is over component layout only, so a body change matches and a
    // field change differs. This is the invariant ReloadInPlace relies on.
    const uint64_t base = ComputeScriptComponentSchemaHash(*BuildModule(kDoor));
    constexpr std::string_view logicEdit =
        "component DoorState {\n"
        "    open: bool = false\n"
        "    locked: bool = true\n"
        "}\n"
        "interaction OpenDoor {\n"
        "    fn interact(ctx: InteractionContext) {\n"
        "        ctx.target.DoorState.locked = false\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(ComputeScriptComponentSchemaHash(*BuildModule(logicEdit)), base);
    constexpr std::string_view fieldEdit =
        "component DoorState {\n"
        "    open: bool = false\n"
        "}\n"
        "interaction OpenDoor {\n"
        "    fn interact(ctx: InteractionContext) {\n"
        "        ctx.target.DoorState.open = true\n"
        "    }\n"
        "}\n";
    EXPECT_NE(ComputeScriptComponentSchemaHash(*BuildModule(fieldEdit)), base);
}

// -- Loader staged/commit (in-memory byte source, no cook) --------------------

namespace
{
    // Minimal IAssetSource over one path's bytes.
    class MemorySource final : public IAssetSource
    {
    public:
        MemorySource(std::string path, std::vector<std::byte> bytes)
            : StoredPath(std::move(path)), Bytes(std::move(bytes)) {}

        bool ReadBytes(std::string_view filePath, std::vector<std::byte>& out) override
        {
            if (filePath != StoredPath)
            {
                return false;
            }
            out = Bytes;
            return true;
        }

    private:
        std::string StoredPath;
        std::vector<std::byte> Bytes;
    };
}

TEST(ScriptAssetLoader, StagesAndCommitsCookedBytes)
{
    LoggingProvider logging;
    ScriptCache cache(logging);
    ScriptAssetLoader loader(logging, &cache);

    const std::vector<std::byte> tbc = WriteScriptModule(*BuildModule(kDoor));
    AssetRecord record;
    record.Type = AssetType::Script;
    record.Path = "scripts/door.t";
    record.FilePath = "scripts/door.t";
    MemorySource source(record.FilePath, tbc);

    AssetStaging staging = loader.LoadStaged(record, source);
    ASSERT_TRUE(staging.IsValid()) << staging.Error;
    const ScriptHandle handle = loader.CommitTyped(std::move(staging));
    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(cache.Find("scripts/door.t").IsValid());
    cache.Release(handle);
}

TEST(ScriptAssetLoader, RejectsCorruptBytecode)
{
    LoggingProvider logging;
    ScriptCache cache(logging);
    ScriptAssetLoader loader(logging, &cache);

    std::vector<std::byte> tbc = WriteScriptModule(*BuildModule(kDoor));
    tbc[0] = std::byte{'X'}; // break the magic
    AssetRecord record;
    record.Type = AssetType::Script;
    record.Path = "scripts/door.t";
    record.FilePath = "scripts/door.t";
    MemorySource source(record.FilePath, tbc);

    const AssetStaging staging = loader.LoadStaged(record, source);
    EXPECT_FALSE(staging.IsValid());
    EXPECT_NE(staging.Error.find("invalid .tbc"), std::string::npos);
}

// -- Cook: importer + import-closure invalidation -----------------------------

#ifdef SENCHA_ENABLE_COOK
namespace
{
    class TempAssets
    {
    public:
        TempAssets()
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_script_cook_" + std::to_string(rd()));
            std::filesystem::create_directories(Root);
        }
        ~TempAssets()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        void Write(std::string_view rel, std::string_view text) const
        {
            const std::filesystem::path full = Root / rel;
            std::filesystem::create_directories(full.parent_path());
            std::ofstream(full, std::ios::binary | std::ios::trunc)
                .write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        [[nodiscard]] std::string PathString() const { return Root.generic_string(); }
        [[nodiscard]] const std::filesystem::path& Path() const { return Root; }

    private:
        std::filesystem::path Root;
    };
}

TEST(ScriptCook, CooksSourceToBytecodeArtifact)
{
    TempAssets assets;
    assets.Write("scripts/door.t", kDoor);

    ScriptImporter importer(assets.PathString());
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(assets.PathString(), importers, registry, logging, &stats));
    EXPECT_EQ(stats.Imported, 1u);

    const AssetRecord* record = registry.FindByPath("asset://scripts/door.t");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::Script);
    EXPECT_TRUE(std::filesystem::is_regular_file(assets.Path() / ".cooked/scripts/door.t.tbc"));
}

TEST(ScriptCook, CompileErrorReportsSourceLocation)
{
    TempAssets assets;
    assets.Write("scripts/bad.t", "fn f(): i32 {\n    return unknown\n}\n");

    ScriptImporter importer(assets.PathString());
    std::string source;
    {
        std::ifstream in(assets.Path() / "scripts/bad.t", std::ios::binary);
        std::ostringstream t;
        t << in.rdbuf();
        source = t.str();
    }
    // Import directly with a discard writer to inspect the error.
    struct DiscardWriter final : ICookOutputWriter
    {
        bool WriteBytes(std::string_view, std::span<const std::byte>) override { return true; }
    } writer;
    const std::vector<std::byte> srcBytes(
        reinterpret_cast<const std::byte*>(source.data()),
        reinterpret_cast<const std::byte*>(source.data()) + source.size());
    const ImportResult result =
        importer.Import(ImportInput{ "scripts/bad.t", srcBytes }, writer);
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("scripts/bad.t:2"), std::string::npos) << result.Error;
}

TEST(ScriptCook, LibraryEditInvalidatesDependents)
{
    TempAssets assets;
    assets.Write("scripts/lib.t",
                 "fn helper(x: i32): i32 {\n    return x + 1\n}\n");
    assets.Write("scripts/main.t",
                 "import \"lib.t\"\n"
                 "behavior B {\n"
                 "    fn fixed(ctx: BehaviorContext) {\n"
                 "        let a = helper(2)\n"
                 "    }\n"
                 "}\n");

    ScriptImporter importer(assets.PathString());
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    {
        AssetRegistry registry(logging);
        ImportOnDemandStats stats;
        ASSERT_TRUE(ImportAssetsOnDemand(assets.PathString(), importers, registry, logging, &stats));
        EXPECT_EQ(stats.Imported, 2u); // lib.t and main.t both cook
    }

    // A warm run recooks nothing.
    {
        AssetRegistry registry(logging);
        ImportOnDemandStats stats;
        ASSERT_TRUE(ImportAssetsOnDemand(assets.PathString(), importers, registry, logging, &stats));
        EXPECT_EQ(stats.Imported, 0u);
        EXPECT_EQ(stats.CookedFresh, 2u);
    }

    // Editing the library changes main.t's dependency hash, so main.t recooks
    // even though its own bytes are unchanged (lib.t recooks on its own hash).
    assets.Write("scripts/lib.t",
                 "fn helper(x: i32): i32 {\n    return x + 100\n}\n");
    {
        AssetRegistry registry(logging);
        ImportOnDemandStats stats;
        ASSERT_TRUE(ImportAssetsOnDemand(assets.PathString(), importers, registry, logging, &stats));
        EXPECT_EQ(stats.Imported, 2u);
    }
}
#endif // SENCHA_ENABLE_COOK
