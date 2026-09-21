// The `authored.bindings` subtype through the real staged data-asset path.
//
// What matters here is that the compiled value is World-independent: it names
// verbs, not ids, and it reports its asset dependencies from a staging worker
// that has no World to resolve anything against. One loaded asset is then
// instantiated into two catalogs and means the right thing in each.

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbBindingData.h>
#include <core/assets/AssetSource.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
class TempBindingFile
{
public:
    TempBindingFile()
    {
        static int counter = 0;
        std::string caseName = "unknown";
        if (const auto* info = testing::UnitTest::GetInstance()->current_test_info())
            caseName = std::string(info->test_suite_name()) + "_" + info->name();

        File = std::filesystem::temp_directory_path()
            / ("sencha_verb_bindings_" + caseName + "_" + std::to_string(++counter) + ".sdata");
    }

    ~TempBindingFile()
    {
        std::error_code ec;
        std::filesystem::remove(File, ec);
    }

    TempBindingFile(const TempBindingFile&) = delete;
    TempBindingFile& operator=(const TempBindingFile&) = delete;

    void Write(std::string_view text) const
    {
        std::ofstream out(File, std::ios::trunc);
        out << text;
    }

    [[nodiscard]] AssetRecord Record() const
    {
        return AssetRecord{
            .Type = AssetType::Data,
            .SourceKind = AssetSourceKind::File,
            .Path = "asset://data/test/bindings.sdata",
            .FilePath = File.generic_string(),
        };
    }

    std::filesystem::path File;
};

class VerbBindingAssetTest : public ::testing::Test
{
protected:
    VerbBindingAssetTest()
        : Loader(Logging, &Types, &Schemas, &Cache)
    {
        RegisterVerbBindingData(Types, Schemas);
    }

    ~VerbBindingAssetTest() override { UnregisterVerbBindingData(Types, Schemas); }

    [[nodiscard]] AssetStaging Stage(std::string_view text)
    {
        File.Write(text);
        return Loader.LoadStaged(File.Record(), Source);
    }

    // The compiled value as the cache would hand it out, without committing:
    // staging is where the World-independent half is decided.
    [[nodiscard]] static const VerbBindingLibrary* Library(const AssetStaging& staged)
    {
        const auto* compiled = std::any_cast<CompiledDataAsset>(&staged.Payload);
        return compiled == nullptr
            ? nullptr
            : static_cast<const VerbBindingLibrary*>(compiled->Value.get());
    }

    LoggingProvider Logging;
    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    DataAssetCache Cache;
    DataAssetLoader Loader;
    FileAssetSource Source;
    TempBindingFile File;
};
}

TEST_F(VerbBindingAssetTest, TheSubtypeRegistersItsSchemaBesideItself)
{
    ASSERT_NE(Types.Find(kVerbBindingsTypeName), nullptr);
    ASSERT_NE(Schemas.Find(kVerbBindingsTypeName), nullptr);
    EXPECT_EQ(Types.Find(kVerbBindingsTypeName)->CurrentVersion, 1u);

    // The argument map's keys are the verb's argument names, which the envelope
    // schema cannot enumerate -- so it lets them through and the compiler
    // checks each against the contract it belongs to.
    EXPECT_TRUE(Schemas.Find(kVerbBindingsTypeName)->AllowUnknownFields);
}

TEST_F(VerbBindingAssetTest, ReferenceKindsAreReportedAsDependenciesWithoutAWorld)
{
    const AssetStaging staged = Stage(R"({
        "type": "authored.bindings",
        "version": 1,
        "data": { "bindings": [ {
            "key": "hit",
            "verb": "test.hit",
            "inputs": ["target"],
            "arguments": {
                "Target": { "input": "target" },
                "Amount": { "const": 5 },
                "Look":   { "asset": "asset://materials/hit.smat" },
                "Tuning": { "data": "asset://data/run.sdata" },
                "Kind":   { "tag": "Score.Pickup" },
                "Anchor": { "entity": "00000000000000ab" }
            }
        } ] }
    })");

    ASSERT_TRUE(staged.IsValid()) << staged.Error;

    // Assets are eager: the binding cannot execute without them, so the asset
    // system is told now. A tag and an entity are World vocabulary and World
    // contents -- neither is content this asset depends on.
    ASSERT_EQ(staged.Dependencies.size(), 2u);
    EXPECT_EQ(staged.Dependencies[0].Path, "asset://materials/hit.smat");
    EXPECT_EQ(staged.Dependencies[1].Path, "asset://data/run.sdata");
    EXPECT_EQ(staged.Dependencies[1].Type, AssetType::Data);

    const VerbBindingLibrary* library = Library(staged);
    ASSERT_NE(library, nullptr);
    ASSERT_EQ(library->Bindings.size(), 1u);

    const VerbBindingDesc& binding = library->Bindings.front();
    EXPECT_EQ(binding.VerbName, "test.hit");
    EXPECT_EQ(binding.KeyId, MakeVerbBindingKey("hit"));
    ASSERT_EQ(binding.Inputs.size(), 1u);
    EXPECT_EQ(binding.Inputs[0], "target");
    ASSERT_EQ(binding.Arguments.size(), 6u);
    EXPECT_EQ(binding.Arguments[0].Source, VerbArgumentSource::Input);
    EXPECT_EQ(binding.Arguments[1].Source, VerbArgumentSource::Literal);
    EXPECT_EQ(binding.Arguments[2].Source, VerbArgumentSource::Asset);
    EXPECT_EQ(binding.Arguments[3].Source, VerbArgumentSource::DataAsset);
    EXPECT_EQ(binding.Arguments[4].Source, VerbArgumentSource::Tag);
    EXPECT_EQ(binding.Arguments[5].Source, VerbArgumentSource::Entity);
    EXPECT_EQ(library->Find("hit"), &binding);
    EXPECT_EQ(library->Find(MakeVerbBindingKey("hit")), &binding);
}

TEST_F(VerbBindingAssetTest, MalformedRecordsAreRefusedWithTheirReason)
{
    const auto refuses = [this](std::string_view body, std::string_view expected) {
        const AssetStaging staged = Stage(body);
        EXPECT_FALSE(staged.IsValid()) << body;
        EXPECT_NE(staged.Error.find(expected), std::string::npos) << staged.Error;
    };

    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","arguments":{"X":{"const":1,"input":"y"}}}]}})",
            "more than one source");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","arguments":{"X":{}}}]}})",
            "no source");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","arguments":{"X":3}}]}})",
            "expected one of");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"not a verb name"}]}})",
            "is not a verb name");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op"},{"key":"a","verb":"test.other"}]}})",
            "more than once");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","inputs":["x","x"]}]}})",
            "declared twice");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","arguments":{"X":{"entity":"nope"}}}]}})",
            "persistent entity");
    refuses(R"({"type":"authored.bindings","version":1,"data":{"bindings":[
        {"key":"a","verb":"test.op","arguments":{"X":{"asset":"materials/hit.smat"}}}]}})",
            "asset://");
}

TEST_F(VerbBindingAssetTest, AFileFromAnotherVersionIsRefusedRatherThanReinterpreted)
{
    const AssetStaging staged = Stage(R"({
        "type": "authored.bindings",
        "version": 2,
        "data": { "bindings": [] }
    })");
    EXPECT_FALSE(staged.IsValid());
    EXPECT_NE(staged.Error.find("version"), std::string::npos) << staged.Error;
}

TEST_F(VerbBindingAssetTest, OneLoadedAssetResolvesIndependentlyInTwoCatalogs)
{
    const AssetStaging staged = Stage(R"({
        "type": "authored.bindings",
        "version": 1,
        "data": { "bindings": [
            { "key": "score", "verb": "test.score", "arguments": { "Amount": { "const": 3 } } }
        ] }
    })");
    ASSERT_TRUE(staged.IsValid()) << staged.Error;
    const VerbBindingLibrary* library = Library(staged);
    ASSERT_NE(library, nullptr);

    DataFieldSchema amount;
    amount.Key = "Amount";
    amount.Kind = DataFieldKind::Int;
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    root.Children.push_back(std::move(amount));

    const auto declare = [&root](VerbRegistry& registry, bool padded) {
        VerbRegistrationScope scope(registry, "test");
        if (padded)
        {
            // Declared first, so the shared verb lands on a different slot here
            // than it does in the other catalog.
            VerbDefinition padding;
            padding.Name = "test.padding";
            (void)scope.Declare(std::move(padding));
        }
        VerbDefinition definition;
        definition.Name = "test.score";
        definition.Arguments = root;
        (void)scope.Declare(std::move(definition));
        return scope.Commit();
    };

    VerbRegistry first;
    VerbRegistry second;
    ASSERT_TRUE(declare(first, false));
    ASSERT_TRUE(declare(second, true));

    std::vector<std::string> errors;
    CompiledVerbBinding here;
    CompiledVerbBinding there;
    ASSERT_TRUE(CompileVerbBinding(library->Bindings.front(),
                                   VerbBindingEnvironment{ .Verbs = &first }, here, errors));
    ASSERT_TRUE(CompileVerbBinding(library->Bindings.front(),
                                   VerbBindingEnvironment{ .Verbs = &second }, there, errors));

    // Different ids, different catalogs, the same authored meaning.
    EXPECT_NE(here.Verb, there.Verb);
    EXPECT_NE(here.Catalog, there.Catalog);
    std::int64_t amountHere = 0;
    std::int64_t amountThere = 0;
    EXPECT_TRUE(here.Constants.TryGetInt(0, amountHere));
    EXPECT_TRUE(there.Constants.TryGetInt(0, amountThere));
    EXPECT_EQ(amountHere, 3);
    EXPECT_EQ(amountThere, 3);
}

TEST_F(VerbBindingAssetTest, ReloadReplacesTheLibraryAndMovesTheReloadVersion)
{
    ASSERT_TRUE(Loader
                    .CommitTyped(Stage(R"({"type":"authored.bindings","version":1,"data":{
                        "bindings":[{"key":"a","verb":"test.before"}]}})"))
                    .IsValid());

    const DataAssetHandle handle = Cache.Find(File.Record().Path);
    ASSERT_TRUE(handle.IsValid());
    const std::uint64_t before = Cache.GetReloadVersion(handle);
    {
        const auto* library =
            Cache.TryGet<VerbBindingLibrary>(handle, std::string(kVerbBindingsTypeName));
        ASSERT_NE(library, nullptr);
        ASSERT_NE(library->Find("a"), nullptr);
        EXPECT_EQ(library->Find("a")->VerbName, "test.before");
    }

    // A reload that removes a binding removes it: the old behaviour must not
    // keep running because the file stopped mentioning it.
    ASSERT_TRUE(Loader.CommitReload(Stage(R"({"type":"authored.bindings","version":1,"data":{
        "bindings":[{"key":"b","verb":"test.after"}]}})")));

    EXPECT_EQ(Cache.Find(File.Record().Path), handle);
    EXPECT_GT(Cache.GetReloadVersion(handle), before);
    const auto* reloaded =
        Cache.TryGet<VerbBindingLibrary>(handle, std::string(kVerbBindingsTypeName));
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->Find("a"), nullptr);
    ASSERT_NE(reloaded->Find("b"), nullptr);

    // An invalid reload leaves the last good revision published.
    EXPECT_FALSE(Loader.CommitReload(Stage(R"({"type":"authored.bindings","version":1,"data":{
        "bindings":[{"key":"c"}]}})")));
    const auto* still =
        Cache.TryGet<VerbBindingLibrary>(handle, std::string(kVerbBindingsTypeName));
    ASSERT_NE(still, nullptr);
    EXPECT_NE(still->Find("b"), nullptr);
}

TEST_F(VerbBindingAssetTest, AnEmptyLibraryIsValidContent)
{
    const AssetStaging staged = Stage(R"({
        "type": "authored.bindings", "version": 1, "data": { "bindings": [] }
    })");
    ASSERT_TRUE(staged.IsValid()) << staged.Error;
    const VerbBindingLibrary* library = Library(staged);
    ASSERT_NE(library, nullptr);
    EXPECT_TRUE(library->Bindings.empty());
    EXPECT_TRUE(staged.Dependencies.empty());
}

TEST_F(VerbBindingAssetTest, ASetBuiltFromAnAssetFollowsItsReloads)
{
    ASSERT_TRUE(Loader
                    .CommitTyped(Stage(R"({"type":"authored.bindings","version":1,"data":{
                        "bindings":[{"key":"a","verb":"test.op"}]}})"))
                    .IsValid());
    const DataAssetHandle handle = Cache.Find(File.Record().Path);
    ASSERT_TRUE(handle.IsValid());

    VerbRegistry registry;
    {
        VerbRegistrationScope scope(registry, "test");
        VerbDefinition op;
        op.Name = "test.op";
        (void)scope.Declare(std::move(op));
        VerbDefinition other;
        other.Name = "test.other";
        (void)scope.Declare(std::move(other));
        ASSERT_TRUE(scope.Commit());
    }
    const VerbBindingEnvironment environment{ .Verbs = &registry };

    VerbBindingSet set;
    std::vector<std::string> errors;
    set.InstantiateFrom(Cache, handle, environment, errors);
    EXPECT_TRUE(errors.empty());
    const std::uint64_t revision = set.Revision();
    ASSERT_NE(set.Find("a"), nullptr);

    // Nothing reloaded: nothing rebuilt, and the revision stays.
    EXPECT_FALSE(set.Refresh(&Cache, environment, errors));
    EXPECT_EQ(set.Revision(), revision);

    // A reload that drops one record and adds another is followed exactly.
    ASSERT_TRUE(Loader.CommitReload(Stage(R"({"type":"authored.bindings","version":1,"data":{
        "bindings":[{"key":"b","verb":"test.other"}]}})")));
    EXPECT_TRUE(set.Refresh(&Cache, environment, errors));
    EXPECT_NE(set.Revision(), revision);
    EXPECT_EQ(set.Find("a"), nullptr);
    ASSERT_NE(set.Find("b"), nullptr);
    EXPECT_EQ(set.Find("b")->Verb, registry.Find("test.other"));
}

TEST_F(VerbBindingAssetTest, ASetRebuildsItselfWhenTheCatalogOrAnInMemoryContributionIsInvolved)
{
    ASSERT_TRUE(Loader
                    .CommitTyped(Stage(R"({"type":"authored.bindings","version":1,"data":{
                        "bindings":[{"key":"a","verb":"test.later"}]}})"))
                    .IsValid());
    const DataAssetHandle handle = Cache.Find(File.Record().Path);
    ASSERT_TRUE(handle.IsValid());

    VerbRegistry registry;
    const VerbBindingEnvironment environment{ .Verbs = &registry };

    // Built while its verb is unknown: the record is unresolved, and the set
    // knows it has something unresolved.
    VerbBindingSet set;
    std::vector<std::string> errors;
    set.InstantiateFrom(Cache, handle, environment, errors);
    EXPECT_EQ(set.Find("a"), nullptr);
    EXPECT_TRUE(set.HasUnresolved());

    // A library handed over in memory is a contribution like any other.
    VerbBindingLibrary native;
    VerbBindingDesc n;
    n.Key = "native";
    n.KeyId = MakeVerbBindingKey(n.Key);
    n.VerbName = "test.native";
    native.Bindings.push_back(std::move(n));
    set.Append(native, environment, errors);
    EXPECT_EQ(set.Find("native"), nullptr);

    // Declaring the verbs is what could make either resolve, and the set sees
    // that on its own: no consumer keeps a retry list.
    {
        VerbRegistrationScope scope(registry, "test");
        VerbDefinition later;
        later.Name = "test.later";
        (void)scope.Declare(std::move(later));
        VerbDefinition nativeVerb;
        nativeVerb.Name = "test.native";
        (void)scope.Declare(std::move(nativeVerb));
        ASSERT_TRUE(scope.Commit());
    }
    errors.clear();
    EXPECT_TRUE(set.Refresh(&Cache, environment, errors));
    ASSERT_NE(set.Find("a"), nullptr);
    ASSERT_NE(set.Find("native"), nullptr) << "the in-memory contribution was lost on rebuild";
    EXPECT_FALSE(set.HasUnresolved());

    // An asset reload rebuilds every contribution, the in-memory one included.
    ASSERT_TRUE(Loader.CommitReload(Stage(R"({"type":"authored.bindings","version":1,"data":{
        "bindings":[{"key":"a2","verb":"test.later"}]}})")));
    EXPECT_TRUE(set.Refresh(&Cache, environment, errors));
    EXPECT_EQ(set.Find("a"), nullptr);
    ASSERT_NE(set.Find("a2"), nullptr);
    ASSERT_NE(set.Find("native"), nullptr);

    // Retiring the provider is a catalog change the set follows too.
    registry.RetireProvider("test");
    EXPECT_TRUE(set.Refresh(&Cache, environment, errors));
    EXPECT_EQ(set.Find("a2"), nullptr);
    EXPECT_TRUE(set.HasUnresolved());
}
