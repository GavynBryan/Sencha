// `candidates.evaluation` assets through the real staged data path, rebinding
// when what they were bound against moves, and the authored-query measure.

#include "CandidateHarness.h"

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <core/assets/AssetSource.h>
#include <core/logging/LoggingProvider.h>
#include <spatial/candidates/CandidateEvaluationData.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <map>

namespace
{
    const ZoneId kRoom{ 1 };

    class CandidateAssetTest : public ::testing::Test
    {
    protected:
        CandidateAssetTest()
            : Loader(Logging, &Types, &Schemas, &Cache)
        {
            RegisterCandidateEvaluationData(Types, Schemas, Harness.Catalogs);
            static int counter = 0;
            File = std::filesystem::temp_directory_path()
                / ("sencha_candidate_evaluation_" + std::to_string(++counter) + ".sdata");
        }

        ~CandidateAssetTest() override
        {
            UnregisterCandidateEvaluationData(Types, Schemas);
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        [[nodiscard]] AssetStaging Stage(std::string_view data)
        {
            std::ofstream(File, std::ios::trunc) << R"({"type":"candidates.evaluation","version":1,"data":)" << data << "}";
            const AssetRecord record{ .Type = AssetType::Data,
                                      .SourceKind = AssetSourceKind::File,
                                      .Path = "asset://data/test/evaluation.sdata",
                                      .FilePath = File.generic_string() };
            return Loader.LoadStaged(record, Source);
        }

        [[nodiscard]] CandidateBindEnvironment Environment() { return { &Harness.Tags, nullptr }; }

        CandidateHarness Harness;
        LoggingProvider Logging;
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        DataAssetCache Cache;
        DataAssetLoader Loader;
        FileAssetSource Source;
        std::filesystem::path File;
    };

    constexpr std::string_view kNear = R"({ "slots": [{ "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.distance", "arguments": { "slot": "querier" }, "require": { "max": 3 } }],
        "selection": { "mode": "all_qualified" } })";
    constexpr std::string_view kFar = R"({ "slots": [{ "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.distance", "arguments": { "slot": "querier" }, "require": { "min": 3 } }],
        "selection": { "mode": "all_qualified" } })";
}

TEST_F(CandidateAssetTest, CompileProblemsFailTheLoadStage)
{
    const AssetStaging staged = Stage(R"({ "generators": [{ "generator": "candidates.generator.nowhere" }] })");
    EXPECT_FALSE(staged.IsValid());
    EXPECT_NE(staged.Error.find("not a generator this host offers"), std::string::npos) << staged.Error;
}

TEST_F(CandidateAssetTest, ABindingFollowsReloadsOfItsAsset)
{
    Harness.AttachBareZone(kRoom);
    ASSERT_TRUE(Loader.CommitTyped(Stage(kNear)).IsValid());
    const DataAssetHandle handle = Cache.Find("asset://data/test/evaluation.sdata");
    ASSERT_TRUE(handle.IsValid());

    CandidateEvaluationBinding binding;
    std::vector<std::string> errors;
    ASSERT_TRUE(binding.BindFrom(Cache, handle, Harness.Catalogs, Environment(), errors))
        << CandidateHarness::Joined(errors);

    const std::array spots{ CandidatePoint{ .Position = Vec3d(1, 0, 0) }, CandidatePoint{ .Position = Vec3d(8, 0, 0) } };
    const std::array slots{ CandidateSlotBinding{ 1, spots } };
    CandidateContext context = QuerierAt(Harness, kRoom, Vec3d::Zero());
    context.Slots = slots;

    ASSERT_EQ(Harness.Run(*binding.Evaluation(), context).Status, CandidateRunStatus::Success);
    ASSERT_EQ(Harness.Returned().size(), 1u);
    EXPECT_EQ(Harness.Returned()[0].Order, 0u);
    EXPECT_FALSE(binding.Refresh(Cache, Harness.Catalogs, Environment(), errors));

    // The reload replaces the description in its slot; the binding sees the
    // version move and rebinds before its next run, on the same thread.
    ASSERT_TRUE(Loader.CommitReload(Stage(kFar)));
    EXPECT_TRUE(binding.Refresh(Cache, Harness.Catalogs, Environment(), errors));
    ASSERT_NE(binding.Evaluation(), nullptr);
    ASSERT_EQ(Harness.Run(*binding.Evaluation(), context).Status, CandidateRunStatus::Success);
    ASSERT_EQ(Harness.Returned().size(), 1u);
    EXPECT_EQ(Harness.Returned()[0].Order, 1u);

    // New tags can make an unknown name resolve, so tag growth rebinds too.
    EXPECT_FALSE(binding.Refresh(Cache, Harness.Catalogs, Environment(), errors));
    (void)Harness.Tags.RegisterTag("test.new");
    EXPECT_TRUE(binding.Refresh(Cache, Harness.Catalogs, Environment(), errors));
}

namespace
{
    struct AmmoTable
    {
        std::map<std::uint32_t, std::int64_t> Rounds;
    };

    AuthoredQueryStatus AmmoAbove(const AmmoTable& table, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId entity;
        std::int64_t minimum = 0;
        if (!arguments[0].TryGetEntity(entity) || !arguments[1].TryGetInt(minimum))
            return AuthoredQueryStatus::InvalidArguments;
        const auto found = table.Rounds.find(entity.Index);
        if (found == table.Rounds.end())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Int(found->second - minimum);
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus AmmoAboveAsFloat(const AmmoTable& table, std::span<const AuthoredValue> arguments,
                                         AuthoredValue& result)
    {
        const AuthoredQueryStatus status = AmmoAbove(table, arguments, result);
        std::int64_t whole = 0;
        if (status == AuthoredQueryStatus::Value && result.TryGetInt(whole))
            result = AuthoredValue::Float(static_cast<double>(whole));
        return status;
    }

    void DeclareAmmo(AuthoredQueryRegistry& registry, DataFieldKind result)
    {
        AuthoredQueryRegistrationScope scope(registry, "test");
        AuthoredQueryDefinition ammo;
        ammo.Name = "test.ammo_above";
        ammo.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "Target", "Target"));
        DataFieldSchema minimum = MakeDataField(DataFieldKind::Int, "Minimum", "Minimum");
        minimum.Required = false;
        minimum.Default = std::int64_t{ 0 };
        ammo.Arguments.Children.push_back(std::move(minimum));
        ammo.Result = MakeDataField(result, "", "Rounds");
        ASSERT_TRUE(scope.Declare(std::move(ammo)));
        ASSERT_TRUE(scope.Commit());
    }

    constexpr std::string_view kAmmoCheck = R"({
        "generators": [{ "generator": "candidates.generator.entities", "arguments": { "tags": { "all": ["pickup.ammo"] } } }],
        "criteria": [{ "measure": "candidates.measure.authored_query",
                       "arguments": { "query": "test.ammo_above", "entity_argument": "Target", "arguments": { "Minimum": 2 } },
                       "require": { "min": 1 } }],
        "selection": { "mode": "all_qualified" }
    })";
}

TEST(CandidateAuthoredQuery, AGameQueryGatesEntityCandidates)
{
    CandidateHarness harness;
    const RuntimeZoneRecord& zone = harness.AttachBareZone(kRoom);
    const EntityId full = harness.AddTagged(Vec3d(1, 0, 0), { "pickup.ammo" }, zone.Partition);
    const EntityId low = harness.AddTagged(Vec3d(2, 0, 0), { "pickup.ammo" }, zone.Partition);
    const EntityId unknown = harness.AddTagged(Vec3d(3, 0, 0), { "pickup.ammo" }, zone.Partition);
    (void)unknown;

    AuthoredQueryRegistry registry;
    DeclareAmmo(registry, DataFieldKind::Int);
    AuthoredQueryDispatcher dispatcher(registry);
    AmmoTable table{ { { full.Index, 6 }, { low.Index, 2 } } };
    const AuthoredQueryBindingToken token = dispatcher.Bind<&AmmoAbove>(registry.Find("test.ammo_above"), table);
    CandidateEvaluator evaluator(harness.Runtime, harness.Catalogs, nullptr, &dispatcher);

    const CandidateEvaluation evaluation = harness.Compile(kAmmoCheck, &registry);
    CandidateTrace trace(CandidateTraceLevel::Full, 8);
    const CandidateRunResult result = evaluator.Evaluate(evaluation, QuerierAt(harness, kRoom, Vec3d::Zero()),
                                                         harness.Scratch, harness.Results, &trace);

    ASSERT_EQ(result.Status, CandidateRunStatus::Success);
    ASSERT_EQ(harness.Returned().size(), 1u);
    EXPECT_EQ(harness.Returned()[0].Entity, full);
    EXPECT_EQ(trace.Value(1, 0).Value, 0.0f);
    EXPECT_EQ(trace.Value(2, 0).Status, CandidateMeasureStatus::NotApplicable);
}

TEST(CandidateAuthoredQuery, AChangedQueryContractMakesTheEvaluationStaleUntilRebound)
{
    CandidateHarness harness;
    const RuntimeZoneRecord& zone = harness.AttachBareZone(kRoom);
    const EntityId full = harness.AddTagged(Vec3d(1, 0, 0), { "pickup.ammo" }, zone.Partition);

    AuthoredQueryRegistry registry;
    DeclareAmmo(registry, DataFieldKind::Int);
    AuthoredQueryDispatcher dispatcher(registry);
    AmmoTable table{ { { full.Index, 6 } } };
    AuthoredQueryBindingToken token = dispatcher.Bind<&AmmoAbove>(registry.Find("test.ammo_above"), table);
    CandidateEvaluator evaluator(harness.Runtime, harness.Catalogs, nullptr, &dispatcher);
    const CandidateEvaluation evaluation = harness.Compile(kAmmoCheck, &registry);
    const CandidateContext context = QuerierAt(harness, kRoom, Vec3d::Zero());

    registry.RetireProvider("test");
    DeclareAmmo(registry, DataFieldKind::Float);
    EXPECT_EQ(evaluator.Evaluate(evaluation, context, harness.Scratch, harness.Results).Status,
              CandidateRunStatus::DefinitionStale);

    token = dispatcher.Bind<&AmmoAboveAsFloat>(registry.Find("test.ammo_above"), table);
    const CandidateEvaluation rebound = harness.Compile(kAmmoCheck, &registry);
    ASSERT_EQ(evaluator.Evaluate(rebound, context, harness.Scratch, harness.Results).Status,
              CandidateRunStatus::Success);
    EXPECT_EQ(harness.Returned()[0].Entity, full);
}
