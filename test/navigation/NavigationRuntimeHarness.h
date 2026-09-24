#pragma once

#include "NavigationFixture.h"

#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <ecs/WorldComponentSchema.h>
#include <navigation/NavLinkState.h>
#include <navigation/NavigationGeometry.h>
#include <navigation/NavigationSystem.h>
#include <physics/components/Collider.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <string_view>

class JobSystem;

// A runtime world with navigation, driven by hand instead of a schedule: zones
// attach and detach through the real residency path, and tests step the
// system's per-tick work themselves.
struct NavRuntimeHarness
{
    NavZoneFixture Fixture;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    NavigationSystem System;
    NavQueryContext Context;

    explicit NavRuntimeHarness(JobSystem* jobs = nullptr)
        : System(Runtime, jobs)
    {
        GameplayTagRegistry& tags = Runtime.Entities().AddResource<GameplayTagRegistry>();
        (void)tags.RegisterTag("navigation.profile.humanoid");
        (void)tags.RegisterTag("navigation.traversal.jump");
    }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        schema.Add<NavLinkState>();
        schema.Add<Collider>();
        schema.Add<WorldTransform>();
        schema.Add<NavigationGeometry>();
        schema.Seal();
        return schema;
    }

    RuntimeZoneRecord& Attach(ZoneId zone, const NavTestGeometry& geometry,
                              std::vector<NavLinkRecord> links = {})
    {
        RuntimeZoneRecord& record = Runtime.BeginZoneImport(zone);
        EXPECT_NE(AttachZoneNavigation(Runtime, record, Fixture.Cook(geometry, std::move(links))),
                  nullptr);
        EXPECT_TRUE(Runtime.PublishZone(zone, ZoneParticipation{ .Logic = true }));
        ProcessResidency();
        return record;
    }

    void Detach(ZoneId zone)
    {
        EXPECT_TRUE(Runtime.RequestDetach(zone));
        Runtime.FlushLifecycleRequests();
        ProcessResidency();
    }

    [[nodiscard]] GameplayTagId Tag(std::string_view name) const
    {
        return std::as_const(Runtime.Entities()).TryGetResource<GameplayTagRegistry>()->FindTag(name);
    }

    [[nodiscard]] NavQueryRequest Request(ZoneId zone,
                                          std::span<const GameplayTagId> capabilities = {}) const
    {
        NavQueryRequest request;
        request.Zone = zone;
        request.Profile = Tag("navigation.profile.humanoid");
        request.Capabilities = capabilities;
        return request;
    }

    NavLocation Project(ZoneId zone, Vec3d point)
    {
        const NavProjectResult result =
            System.Queries().ProjectPoint(Context, Request(zone), point, Vec3d(1, 2, 1));
        EXPECT_EQ(result.Status, NavStatus::Success);
        return result.Location;
    }

    NavStatus Reachable(ZoneId zone, Vec3d from, Vec3d to)
    {
        return System.Queries().Reachable(Context, Request(zone), Project(zone, from),
                                          Project(zone, to));
    }

    // A tagged box collider: runtime navigation geometry.
    EntityId AddBox(Vec3d center, Vec3d halfExtents)
    {
        World& world = Runtime.Entities();
        const EntityId entity = world.CreateEntity();
        Collider collider;
        collider.Shape = CollisionShape::MakeBox(halfExtents);
        world.AddComponent(entity, collider);
        Transform3f transform = Transform3f::Identity();
        transform.Position = center;
        world.AddComponent(entity, WorldTransform{ transform });
        world.AddComponent(entity, NavigationGeometry{});
        return entity;
    }

    void Move(EntityId entity, Vec3d center)
    {
        Runtime.Entities().TryGet<WorldTransform>(entity)->Value.Position = center;
    }

    // Detects changes, then rebuilds until no dirty tile remains.
    void Settle()
    {
        System.DetectGeometryChanges();
        do
            System.RebuildDirtyTiles();
        while (System.PendingDirtyTiles() > 0);
    }

private:
    void ProcessResidency()
    {
        EngineConfig config;
        ZoneResidencyContext ctx{ config, Runtime.Entities(), Runtime.BeginResidencyProcessing() };
        System.ZoneResidency(ctx);
        Runtime.FinalizeResidencyProcessing();
    }
};
