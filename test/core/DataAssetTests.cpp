#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace
{
    struct TestDefinition
    {
        double Speed = 0.0;
        std::string Label;
    };

    DataSchema MakeSchema()
    {
        DataFieldSchema speed;
        speed.Key = "speed";
        speed.DisplayName = "Speed";
        speed.Summary = "Movement speed.";
        speed.Description = "The authored speed used by the test definition.";
        speed.Units = "m/s";
        speed.Kind = DataFieldKind::Float;
        speed.Numeric.Minimum = 0.0;
        speed.Numeric.Maximum = 100.0;

        DataFieldSchema label;
        label.Key = "label";
        label.DisplayName = "Label";
        label.Kind = DataFieldKind::String;

        DataFieldSchema dependency;
        dependency.Key = "dependency";
        dependency.DisplayName = "Dependency";
        dependency.Kind = DataFieldKind::AssetRef;
        dependency.Required = false;

        DataFieldSchema root;
        root.Kind = DataFieldKind::Record;
        root.Children = { std::move(speed), std::move(label), std::move(dependency) };

        DataSchema schema;
        schema.TypeName = "test.definition";
        schema.DisplayName = "Test Definition";
        schema.Description = "Headless structured data test subtype.";
        schema.Root = std::move(root);
        return schema;
    }

    DataAssetTypeRegistration MakeType()
    {
        DataAssetTypeRegistration registration;
        registration.Name = "test.definition";
        registration.CurrentVersion = 1;
        registration.Compile = [](const JsonValue& data)
        {
            DataAssetCompileResult result;
            const JsonValue* speed = data.Find("speed");
            const JsonValue* label = data.Find("label");
            if (speed == nullptr || label == nullptr)
            {
                result.Error = "required test fields are missing";
                return result;
            }

            auto compiled = std::make_shared<TestDefinition>();
            compiled->Speed = speed->AsNumber();
            compiled->Label = label->AsString();
            result.Value = std::move(compiled);

            if (const JsonValue* dependency = data.Find("dependency"))
            {
                result.Dependencies.push_back(AssetRef{
                    .Type = AssetType::Material,
                    .Path = dependency->AsString(),
                });
            }
            return result;
        };
        return registration;
    }

    class TempDataFile
    {
    public:
        TempDataFile()
        {
            // ctest runs each case in its own process, so a per-process
            // counter alone repeats the same name in every case and two cases
            // running in parallel share one file. The case name disambiguates.
            static int counter = 0;
            std::string caseName = "unknown";
            if (const auto* info = testing::UnitTest::GetInstance()->current_test_info())
                caseName = std::string(info->test_suite_name()) + "_" + info->name();

            File = std::filesystem::temp_directory_path() /
                   ("sencha_data_asset_" + caseName + "_"
                    + std::to_string(++counter) + ".sdata");
        }

        ~TempDataFile()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        void Write(std::string_view text) const
        {
            std::ofstream out(File, std::ios::trunc);
            out << text;
        }

        AssetRecord Record() const
        {
            return AssetRecord{
                .Type = AssetType::Data,
                .SourceKind = AssetSourceKind::File,
                .Path = "asset://data/test/definition.sdata",
                .FilePath = File.generic_string(),
            };
        }

        std::filesystem::path File;
    };
}

TEST(DataSchema, ReportsExactPathsAndConstraints)
{
    const DataSchema schema = MakeSchema();
    const JsonValue value = JsonValue::Object{
        { "speed", JsonValue(120.0) },
        { "label", JsonValue("too fast") },
        { "unknown", JsonValue(true) },
    };

    std::vector<DataValidationError> errors;
    EXPECT_FALSE(ValidateDataAgainstSchema(value, schema, errors));
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0].Path, "$.speed");
    EXPECT_EQ(errors[1].Path, "$.unknown");
}

TEST(DataAsset, LoadsTypedValueAndReportsDependencies)
{
    LoggingProvider logging;
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    DataAssetCache cache;
    ASSERT_TRUE(types.Register(MakeType()));
    ASSERT_TRUE(schemas.Register(MakeSchema()));

    DataAssetLoader loader(logging, &types, &schemas, &cache);
    FileAssetSource source;
    TempDataFile file;
    file.Write(R"({
        "type": "test.definition",
        "version": 1,
        "data": {
            "speed": 8.5,
            "label": "runner",
            "dependency": "asset://materials/test/runner.smat"
        }
    })");

    AssetStaging staged = loader.LoadStaged(file.Record(), source);
    ASSERT_TRUE(staged.IsValid()) << staged.Error;
    ASSERT_EQ(staged.Dependencies.size(), 1u);
    EXPECT_EQ(staged.Dependencies[0].Type, AssetType::Material);

    // The lease is what generic orchestration would hold; taking it here
    // pins the same reference the commit created.
    AssetLease committed =
        AssetLease::Adopt(AssetType::Data, cache,
                          loader.CommitTyped(std::move(staged)).ToToken());
    ASSERT_TRUE(committed.IsValid());

    const DataAssetHandle handle = cache.Find(file.Record().Path);
    ASSERT_TRUE(handle.IsValid());
    const TestDefinition* value = cache.TryGet<TestDefinition>(handle, "test.definition");
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(value->Speed, 8.5);
    EXPECT_EQ(value->Label, "runner");
    EXPECT_EQ(cache.GetReloadVersion(handle), 1u);

    // The lease is the only reference: dropping it frees the entry.
    committed.Reset();
    EXPECT_FALSE(cache.Find(file.Record().Path).IsValid());
}

TEST(DataAsset, HotReloadPreservesHandleAndIncrementsVersion)
{
    LoggingProvider logging;
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    DataAssetCache cache;
    ASSERT_TRUE(types.Register(MakeType()));
    ASSERT_TRUE(schemas.Register(MakeSchema()));

    DataAssetLoader loader(logging, &types, &schemas, &cache);
    FileAssetSource source;
    TempDataFile file;
    file.Write(R"({"type":"test.definition","version":1,
                   "data":{"speed":4.0,"label":"before"}})");

    ASSERT_TRUE(loader.CommitTyped(loader.LoadStaged(file.Record(), source)).IsValid());
    const DataAssetHandle handle = cache.Find(file.Record().Path);
    ASSERT_TRUE(handle.IsValid());

    file.Write(R"({"type":"test.definition","version":1,
                   "data":{"speed":12.0,"label":"after"}})");
    AssetStaging reload = loader.LoadStaged(file.Record(), source);
    ASSERT_TRUE(reload.IsValid()) << reload.Error;
    ASSERT_TRUE(loader.CommitReload(std::move(reload)));

    EXPECT_EQ(cache.Find(file.Record().Path), handle);
    EXPECT_EQ(cache.GetReloadVersion(handle), 2u);
    const TestDefinition* value = cache.TryGet<TestDefinition>(handle, "test.definition");
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(value->Speed, 12.0);
    EXPECT_EQ(value->Label, "after");
}

TEST(DataAsset, RejectsUnknownSubtypeAndSchemaViolation)
{
    LoggingProvider logging;
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    DataAssetCache cache;
    ASSERT_TRUE(types.Register(MakeType()));
    ASSERT_TRUE(schemas.Register(MakeSchema()));

    DataAssetLoader loader(logging, &types, &schemas, &cache);
    FileAssetSource source;
    TempDataFile file;

    file.Write(R"({"type":"missing.type","version":1,"data":{}})");
    AssetStaging unknown = loader.LoadStaged(file.Record(), source);
    EXPECT_FALSE(unknown.IsValid());
    EXPECT_NE(unknown.Error.find("unknown data asset subtype"), std::string::npos);

    file.Write(R"({"type":"test.definition","version":1,
                   "data":{"speed":200.0,"label":"invalid"}})");
    AssetStaging invalid = loader.LoadStaged(file.Record(), source);
    EXPECT_FALSE(invalid.IsValid());
    EXPECT_NE(invalid.Error.find("$.speed"), std::string::npos);
}

TEST(DataAssetKind, RegisteringTheKindMakesItVisibleThroughTheFrontDoor)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    DataAssetCache cache;
    ASSERT_TRUE(types.Register(MakeType()));
    ASSERT_TRUE(schemas.Register(MakeSchema()));

    DataAssetLoader loader(logging, &types, &schemas, &cache);

    // No caches: the front door knows nothing about structured data until the
    // kind is registered, which is what a game-defined kind relies on.
    AssetSystem assets(logging, registry, nullptr, nullptr);
    EXPECT_EQ(assets.LoaderFor(AssetType::Data), nullptr);

    AssetKindRegistration kind = MakeBuiltinAssetKind(AssetType::Data);
    kind.Stager = &loader;
    kind.Store = &cache;
    kind.Commit = [&](AssetStaging&& staged) -> AssetLease
    {
        const DataAssetHandle handle = loader.CommitTyped(std::move(staged));
        if (!handle.IsValid())
            return {};
        return AssetLease::Adopt(AssetType::Data, cache, handle.ToToken());
    };
    ASSERT_TRUE(assets.Kinds().Register(std::move(kind)));

    TempDataFile file;
    file.Write(R"({"type":"test.definition","version":1,
                   "data":{"speed":3.0,"label":"registered"}})");
    ASSERT_TRUE(registry.Register(file.Record()));

    // Stage and commit entirely through the front door: no caller names
    // DataAssetLoader or DataAssetCache.
    IAssetStager* stager = assets.LoaderFor(AssetType::Data);
    ASSERT_NE(stager, nullptr);
    EXPECT_FALSE(assets.IsResident(file.Record().Path, AssetType::Data));

    AssetLease lease = assets.Commit(stager->LoadStaged(file.Record(), assets.DefaultSource()));
    ASSERT_TRUE(lease.IsValid());
    EXPECT_TRUE(assets.IsResident(file.Record().Path, AssetType::Data));

    // The extension registers with the kind, so a scan classifies .sdata too.
    EXPECT_EQ(assets.Kinds().FindTypeByExtension(".sdata"), AssetType::Data);

    lease.Reset();
    EXPECT_FALSE(assets.IsResident(file.Record().Path, AssetType::Data));
}

// The envelope peek: what a picker asks before an asset has been chosen, and
// so before it can be loaded, validated, or made resident.
TEST(DataAssetSubtype, ReadsTheDeclaredTypeAndNothingElse)
{
    struct Case
    {
        const char* Name;
        std::string_view Json;
        const char* Expected;
    };
    const Case cases[] = {
        { "envelope", R"({"type":"movement.profile","version":1,"data":{}})",
          "movement.profile" },
        { "type only", R"({"type":"input.profile"})", "input.profile" },
        { "no type", R"({"version":1,"data":{}})", "" },
        { "type is not a string", R"({"type":7})", "" },
        { "root is not an object", R"(["movement.profile"])", "" },
        { "malformed", R"({"type":)", "" },
        { "empty", "", "" },
    };

    for (const Case& c : cases)
        EXPECT_EQ(PeekDataAssetSubtype(c.Json), c.Expected) << c.Name;
}

// The same answer read through the asset source, which is how an authoring
// surface asks it about a registered but unloaded asset.
TEST(DataAssetSubtype, ReadsThroughTheAssetSource)
{
    FileAssetSource source;
    TempDataFile file;
    file.Write(R"({"type":"test.definition","version":1,"data":{}})");
    EXPECT_EQ(PeekDataAssetSubtype(source, file.Record()), "test.definition");

    AssetRecord missing = file.Record();
    missing.FilePath = "/nonexistent/sencha/peek.sdata";
    EXPECT_EQ(PeekDataAssetSubtype(source, missing), "");
}
