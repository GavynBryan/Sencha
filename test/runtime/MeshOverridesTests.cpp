#include <gtest/gtest.h>

#include <core/assets/AssetStoreTable.h>
#include <ecs/World.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/MeshOverrides.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <array>
#include <vector>

namespace
{
    // A three-slot body: skin, hair, cloth, one section each.
    struct OverrideFixture
    {
        MaterialCache Materials;
        MaterialSetCache Sets{ &Materials };
        World Entities;
        GpuStaticMesh Mesh;
        MaterialHandle Skin, Hair, Cloth, Gold, Glow;
        MaterialSetHandle Authored;
        EntityId Body;

        OverrideFixture()
        {
            AssetStoreTable stores;
            stores.Add(AssetType::Material, AssetArity::Single, Materials);
            stores.Add(AssetType::Material, AssetArity::List, Sets);
            Entities.SetResource(stores);
            Entities.RegisterComponent<StaticMeshComponent>();
            Entities.RegisterComponent<SkinnedMeshComponent>();
            Entities.RegisterComponent<MeshOverrides>();

            for (MaterialHandle* material : { &Skin, &Hair, &Cloth, &Gold, &Glow })
                *material = Materials.Create(Material{});
            const std::array<MaterialHandle, 3> slots{ Skin, Hair, Cloth };
            Authored = Sets.Acquire(slots);
            for (std::uint32_t slot = 0; slot < 3; ++slot)
                Mesh.Sections.push_back({ .IndexOffset = slot * 3, .IndexCount = 3, .MaterialSlot = slot });

            Body = Entities.CreateEntity();
            StaticMeshComponent mesh;
            mesh.Materials = Authored;
            Entities.AddComponent(Body, mesh);
        }

        [[nodiscard]] EffectiveMeshDraw Drawn() const
        {
            const World& reader = Entities;
            return ApplyMeshOverrides(reader.TryGet<MeshOverrides>(Body), Authored, 0xFFFFFFFFu, Mesh, 3);
        }

        [[nodiscard]] std::vector<MaterialHandle> DrawnSlots() const
        {
            const std::vector<MaterialHandle>* slots = Sets.Get(Drawn().Materials);
            return slots != nullptr ? *slots : std::vector<MaterialHandle>{};
        }
    };

    PresentationOwner Owner(std::uint32_t writer, std::uint64_t key = 1)
    {
        return { .Writer = PresentationWriterId{ writer }, .Holder = LeaseOwner{ .Entity = {}, .Key = key } };
    }

    MeshSlotSelector Drawing(MaterialHandle material)
    {
        return { .Kind = MeshSlotSelector::By::DrawnMaterial, .Material = material, .Slot = 0 };
    }
}

TEST(MeshOverrides, AHideByDrawnMaterialHidesItsSectionsAndClearingRestoresExactly)
{
    OverrideFixture fx;
    const EffectiveMeshDraw before = fx.Drawn();

    const MeshOverrideOutcome outcome =
        SetMeshOverride(fx.Entities, fx.Body, { .Owner = Owner(1), .Order = 0, .Hide = { Drawing(fx.Hair) }, .Substitute = {} });
    EXPECT_EQ(outcome.Result, MeshOverrideResult::Applied);
    EXPECT_EQ(fx.Drawn().SectionMask, 0xFFFFFFFFu & ~0b010u) << "only the section drawing the hair";
    EXPECT_EQ(fx.Drawn().Materials, fx.Authored) << "a hide substitutes nothing";

    ClearMeshOverride(fx.Entities, fx.Body, Owner(1));
    EXPECT_EQ(fx.Drawn().SectionMask, before.SectionMask);
    EXPECT_EQ(fx.Drawn().Materials, before.Materials);
    EXPECT_TRUE(fx.Entities.HasComponent<MeshOverrides>(fx.Body)) << "kept, so contributing again moves nothing";
    EXPECT_EQ(fx.Entities.TryGet<StaticMeshComponent>(fx.Body)->Materials, fx.Authored)
        << "the authored state is never written";
}

TEST(MeshOverrides, HidesFromIndependentWritersUnion)
{
    OverrideFixture fx;
    (void)SetMeshOverride(fx.Entities, fx.Body, { .Owner = Owner(1), .Order = 0, .Hide = { Drawing(fx.Hair) }, .Substitute = {} });
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(2), .Order = 0,
                            .Hide = { { .Kind = MeshSlotSelector::By::Slot, .Material = {}, .Slot = 2 } }, .Substitute = {} });
    EXPECT_EQ(fx.Drawn().SectionMask & 0b111u, 0b001u);
    ClearMeshOverride(fx.Entities, fx.Body, Owner(1));
    EXPECT_EQ(fx.Drawn().SectionMask & 0b111u, 0b011u) << "the other writer's hide stays";
}

TEST(MeshOverrides, TheHigherOrderTakesASlotAndTheOtherReturnsWhenItGoes)
{
    OverrideFixture fx;
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(1), .Order = 0, .Hide = {}, .Substitute = { { Drawing(fx.Skin), fx.Gold } } });
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(2), .Order = 5, .Hide = {}, .Substitute = { { Drawing(fx.Skin), fx.Glow } } });
    EXPECT_EQ(fx.DrawnSlots(), (std::vector<MaterialHandle>{ fx.Glow, fx.Hair, fx.Cloth }));

    ClearMeshOverride(fx.Entities, fx.Body, Owner(2));
    EXPECT_EQ(fx.DrawnSlots(), (std::vector<MaterialHandle>{ fx.Gold, fx.Hair, fx.Cloth }));
}

TEST(MeshOverrides, ATieIsReportedAndSettledTheSameWayInAnyOrder)
{
    const auto winnerFor = [](bool lowerFirst) {
        OverrideFixture fx;
        const MeshOverrideContribution low{ .Owner = Owner(1), .Order = 3, .Hide = {},
                                            .Substitute = { { Drawing(fx.Skin), fx.Gold } } };
        const MeshOverrideContribution high{ .Owner = Owner(2), .Order = 3, .Hide = {},
                                             .Substitute = { { Drawing(fx.Skin), fx.Glow } } };
        (void)SetMeshOverride(fx.Entities, fx.Body, lowerFirst ? low : high);
        const MeshOverrideOutcome outcome = SetMeshOverride(fx.Entities, fx.Body, lowerFirst ? high : low);
        EXPECT_TRUE(outcome.Tied);
        EXPECT_EQ(outcome.TieWinner, Owner(1));
        return fx.DrawnSlots()[0] == fx.Gold;
    };
    EXPECT_TRUE(winnerFor(true));
    EXPECT_TRUE(winnerFor(false));
}

TEST(MeshOverrides, ASubstituteStaysResidentWhileContributed)
{
    OverrideFixture fx;
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(1), .Order = 0, .Hide = {}, .Substitute = { { Drawing(fx.Skin), fx.Gold } } });
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(2), .Order = 9, .Hide = {}, .Substitute = { { Drawing(fx.Skin), fx.Glow } } });
    fx.Materials.Release(fx.Gold);
    EXPECT_NE(fx.Materials.Get(fx.Gold), nullptr) << "losing the slot does not free what it would draw";

    ClearMeshOverride(fx.Entities, fx.Body, Owner(2));
    EXPECT_EQ(fx.DrawnSlots()[0], fx.Gold);
    ClearMeshOverride(fx.Entities, fx.Body, Owner(1));
    EXPECT_EQ(fx.Materials.Get(fx.Gold), nullptr) << "and taking it back releases it";
}

TEST(MeshOverrides, TheComposedSetIsReleasedWhenTheEntityGoes)
{
    OverrideFixture fx;
    (void)SetMeshOverride(fx.Entities, fx.Body,
                          { .Owner = Owner(1), .Order = 0, .Hide = {}, .Substitute = { { Drawing(fx.Skin), fx.Gold } } });
    const MaterialSetHandle composed = fx.Drawn().Materials;
    ASSERT_NE(composed, fx.Authored);
    ASSERT_NE(fx.Sets.Get(composed), nullptr);
    fx.Materials.Release(fx.Gold);

    fx.Entities.DestroyEntity(fx.Body);
    EXPECT_EQ(fx.Sets.Get(composed), nullptr);
    EXPECT_EQ(fx.Materials.Get(fx.Gold), nullptr);
    EXPECT_TRUE(fx.Entities.GetResource<MeshOverrideContributions>().ByEntity.empty());
}

TEST(MeshOverrides, AChangedAuthoredSetIsDrawnAsAuthoredUntilComposedAgain)
{
    OverrideFixture fx;
    (void)SetMeshOverride(fx.Entities, fx.Body, { .Owner = Owner(1), .Order = 0, .Hide = { Drawing(fx.Hair) }, .Substitute = {} });

    const std::array<MaterialHandle, 3> reordered{ fx.Hair, fx.Skin, fx.Cloth };
    const MaterialSetHandle changed = fx.Sets.Acquire(reordered);
    MeshOverrides& overrides = (*fx.Entities.TryGet<MeshOverrides>(fx.Body));
    EXPECT_EQ(ApplyMeshOverrides(&overrides, changed, 0b111u, fx.Mesh, 3).SectionMask, 0b111u)
        << "a stale composition never hides the wrong section";

    StaticMeshComponent& mesh = (*fx.Entities.TryGet<StaticMeshComponent>(fx.Body));
    fx.Sets.Retain(changed);
    fx.Sets.Release(mesh.Materials);
    mesh.Materials = changed;
    (void)ComposeMeshOverrides(fx.Entities, fx.Body);
    EXPECT_EQ(ApplyMeshOverrides(fx.Entities.TryGet<MeshOverrides>(fx.Body), changed, 0b111u, fx.Mesh, 3)
                  .SectionMask,
              0b110u)
        << "the hair now draws from slot 0";
    fx.Sets.Release(changed);
}

TEST(MeshOverrides, ASlotPastTheSetIsReportedAndNamesNothing)
{
    OverrideFixture fx;
    const MeshOverrideOutcome outcome = SetMeshOverride(
        fx.Entities, fx.Body,
        { .Owner = Owner(1), .Order = 0, .Hide = { { .Kind = MeshSlotSelector::By::Slot, .Material = {}, .Slot = 7 } },
          .Substitute = {} });
    EXPECT_TRUE(outcome.SlotOutOfRange);
    EXPECT_EQ(fx.Drawn().SectionMask, 0xFFFFFFFFu);
}

TEST(MeshOverrides, AnEntityWithNoMeshIsRefusedAndKeepsItsArchetype)
{
    OverrideFixture fx;
    const EntityId bare = fx.Entities.CreateEntity();
    const MeshOverrideOutcome outcome =
        SetMeshOverride(fx.Entities, bare, { .Owner = Owner(1), .Order = 0, .Hide = { Drawing(fx.Hair) }, .Substitute = {} });
    EXPECT_EQ(outcome.Result, MeshOverrideResult::NoMesh);
    EXPECT_FALSE(fx.Entities.HasComponent<MeshOverrides>(bare));
}
