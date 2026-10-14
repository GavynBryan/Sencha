#pragma once

#include "../../navigation/NavigationFixture.h"

#include <app/GameContexts.h>
#include <spatial/candidates/CandidateEvaluator.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavLinkState.h>
#include <navigation/NavigationGeometry.h>
#include <navigation/NavigationSystem.h>
#include <navigation/ZoneNavigation.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A runtime world with cooked zone navigation, a physics scene the tests fill
// by hand, the engine's candidate operations, and one evaluator over them.
struct CandidateHarness
{
    NavZoneFixture Fixture;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    NavigationSystem Navigation{ Runtime, nullptr };
    PhysicsWorld Physics;
    CandidateCatalogs Catalogs;
    GameplayTagRegistry& Tags = Runtime.Entities().AddResource<GameplayTagRegistry>();
    CandidateEvaluator Evaluator{ Runtime, Catalogs, &Physics, nullptr };
    CandidateScratch Scratch;
    CandidateResultBuffer Results{ 32 };

    CandidateHarness()
    {
        (void)Tags.RegisterTag("navigation.profile.humanoid");
        (void)Tags.RegisterTag("navigation.traversal.jump");
    }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        schema.Add<NavLinkState>();
        schema.Add<Collider>();
        schema.Add<WorldTransform>();
        schema.Add<NavigationGeometry>();
        schema.Add<GameplayTagContainer>();
        schema.Seal();
        return schema;
    }

    RuntimeZoneRecord& AttachZone(ZoneId zone, const NavTestGeometry& geometry, std::vector<NavLinkRecord> links = {})
    {
        RuntimeZoneRecord& record = Runtime.BeginZoneImport(zone);
        EXPECT_NE(AttachZoneNavigation(Runtime, record, Fixture.Cook(geometry, std::move(links))), nullptr);
        EXPECT_TRUE(Runtime.PublishZone(zone, ZoneParticipation{ .Logic = true }));
        ProcessResidency();
        return record;
    }

    // Resident, with no navigation at all.
    RuntimeZoneRecord& AttachBareZone(ZoneId zone)
    {
        RuntimeZoneRecord& record = Runtime.BeginZoneImport(zone);
        EXPECT_TRUE(Runtime.PublishZone(zone, ZoneParticipation{ .Logic = true }));
        ProcessResidency();
        return record;
    }

    EntityId AddTagged(Vec3d position,
                       std::initializer_list<std::string_view> tags,
                       StoragePartitionId partition = PersistentStoragePartition)
    {
        World& world = Runtime.Entities();
        const EntityId entity = world.CreateEntity(partition);
        Transform3f transform = Transform3f::Identity();
        transform.Position = position;
        world.AddComponent(entity, WorldTransform{ transform });
        GameplayTagContainer container;
        for (const std::string_view tag : tags)
            EXPECT_TRUE(container.Grant(*Tags.RegisterTag(tag)));
        world.AddComponent(entity, container);
        return entity;
    }

    EntityId AddPlaced(Vec3d position, Quatf rotation = Quatf::Identity())
    {
        World& world = Runtime.Entities();
        const EntityId entity = world.CreateEntity();
        Transform3f transform = Transform3f::Identity();
        transform.Position = position;
        transform.Rotation = rotation;
        world.AddComponent(entity, WorldTransform{ transform });
        return entity;
    }

    void AddStaticBox(Vec3d min, Vec3d max, EntityId entity = {}, bool trigger = false)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox((max - min) * 0.5f);
        body.Position = (min + max) * 0.5f;
        body.Motion = BodyMotion::Static;
        body.Layer = trigger ? CollisionLayer::Trigger : CollisionLayer::Static;
        body.IsTrigger = trigger;
        body.UserData = PackEntity(entity);
        (void)Physics.AddBody(body);
    }

    [[nodiscard]] NavQueryRequest Request(ZoneId zone, std::span<const GameplayTagId> capabilities = {}) const
    {
        NavQueryRequest request;
        request.Zone = zone;
        request.Profile = Tags.FindTag("navigation.profile.humanoid");
        request.Capabilities = capabilities;
        return request;
    }

    [[nodiscard]] CandidateEvaluationDesc CompileDesc(std::string_view json)
    {
        JsonParseError parseError;
        const std::optional<JsonValue> data = JsonParse(json, &parseError);
        EXPECT_TRUE(data.has_value()) << parseError.Message;
        CandidateEvaluationDesc desc;
        std::vector<std::string> errors;
        EXPECT_TRUE(data && CompileCandidateEvaluationDesc(*data, Catalogs, desc, errors)) << Joined(errors);
        return desc;
    }

    [[nodiscard]] CandidateEvaluation Compile(std::string_view json, const AuthoredQueryRegistry* queries = nullptr)
    {
        const CandidateEvaluationDesc desc = CompileDesc(json);
        CandidateEvaluation evaluation;
        std::vector<std::string> errors;
        EXPECT_TRUE(BindCandidateEvaluation(desc, Catalogs, CandidateBindEnvironment{ &Tags, queries }, evaluation, errors))
            << Joined(errors);
        return evaluation;
    }

    [[nodiscard]] std::vector<std::string> CompileErrors(std::string_view json)
    {
        CandidateEvaluationDesc desc;
        std::vector<std::string> errors;
        const std::optional<JsonValue> data = JsonParse(json);
        EXPECT_TRUE(data.has_value());
        EXPECT_FALSE(CompileCandidateEvaluationDesc(*data, Catalogs, desc, errors));
        return errors;
    }

    CandidateRunResult Run(const CandidateEvaluation& evaluation,
                           const CandidateContext& context,
                           CandidateTrace* trace = nullptr)
    {
        return Evaluator.Evaluate(evaluation, context, Scratch, Results, trace);
    }

    [[nodiscard]] std::span<const CandidateResultEntry> Returned() const { return Results.Entries(); }

    static std::string Joined(const std::vector<std::string>& errors)
    {
        std::string joined;
        for (const std::string& error : errors)
            joined += error + "\n";
        return joined;
    }

private:
    void ProcessResidency()
    {
        EngineConfig config;
        ZoneResidencyContext ctx{ config, Runtime.Entities(), Runtime.BeginResidencyProcessing() };
        Navigation.ZoneResidency(ctx);
        Runtime.FinalizeResidencyProcessing();
    }
};

// A context for a querier standing at `origin` in `zone`, with no entity.
inline CandidateContext QuerierAt(CandidateHarness& harness, ZoneId zone, Vec3d origin)
{
    CandidateContext context;
    context.Origin = origin;
    context.Navigation = harness.Request(zone);
    return context;
}
