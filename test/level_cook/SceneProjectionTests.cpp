// The instance projection: placement records expand into derived live
// entities on load, live edits fold back into the records on save, and the
// two directions agree -- what a fresh document loads from a saved text is
// what the editing session meant.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    class SceneProjectionTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_projection_"
                   + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root / "props");

            // The door prop: a brush body with a light beneath it, authored
            // through a real document so the source is exactly what an editor
            // writes, sidecar included.
            EditorDocument door(Logging);
            const EntityId body = door.GetScene().CreateBrush(Vec3d{ 0, 1, 0 });
            const EntityId light = door.GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
            PointLightComponent lamp{};
            lamp.Intensity = 5.0f;
            lamp.Range = 3.0f;
            door.GetScene().GetRegistry().Components.AddComponent(light, lamp);
            ASSERT_TRUE(door.GetScene().SetParent(light, body));
            ASSERT_TRUE(door.SaveAs((Root / "props/door.sscene").generic_string()));

            BodyId = IdOf(door, body);
            LightId = IdOf(door, light);
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        [[nodiscard]] static PersistentEntityId IdOf(const EditorDocument& doc,
                                                     EntityId entity)
        {
            const auto* id =
                doc.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
            return id != nullptr ? id->Id : PersistentEntityId{};
        }

        [[nodiscard]] static EntityId FindById(const EditorDocument& doc,
                                               PersistentEntityId id)
        {
            for (EntityId entity : doc.GetScene().GetAllEntities())
                if (IdOf(doc, entity) == id)
                    return entity;
            return {};
        }

        // A host scene text placing the door once at x = 5 with recorded ids.
        [[nodiscard]] std::string HostText() const
        {
            return std::string(R"({
  format_version: 1,
  entities: [ { id: '00000000000000aa', components: {} } ],
  instances: [
    { id: '00000000000000f0', parent: '00000000000000aa',
      source: 'asset://props/door.sscene',
      transform: { position: [5, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      entity_ids: { ')")
                + PersistentEntityIdToString(BodyId) + R"(': '0000000000000201',
                    ')" + PersistentEntityIdToString(LightId)
                + R"(': '0000000000000202' } },
  ],
})";
        }

        [[nodiscard]] EditorDocument LoadHost(std::string_view text)
        {
            EditorDocument host(Logging);
            host.SetContentRoots({ Root });
            EXPECT_TRUE(host.LoadFromSceneText(text));
            return host;
        }

        LoggingProvider Logging;
        fs::path Root;
        PersistentEntityId BodyId;
        PersistentEntityId LightId;
    };

    TEST_F(SceneProjectionTest, LoadExpandsThePlacementUnderItsRoot)
    {
        EditorDocument host = LoadHost(HostText());
        EXPECT_TRUE(host.GetProjectionDiagnostics().Clean());
        ASSERT_EQ(host.GetScene().GetEntityCount(), 4u); // local + root + body + light

        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        const EntityId body = FindById(host, PersistentEntityId{ 0x201 });
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        ASSERT_TRUE(root.IsValid());
        ASSERT_TRUE(body.IsValid());
        ASSERT_TRUE(light.IsValid());

        // Parentage: local <- root <- body <- light; placement on the root.
        EXPECT_EQ(host.GetScene().GetParent(root),
                  FindById(host, PersistentEntityId{ 0xaa }));
        EXPECT_EQ(host.GetScene().GetParent(body), root);
        EXPECT_EQ(host.GetScene().GetParent(light), body);
        EXPECT_EQ(host.GetScene().TryGetLocalTransform(root)->Position.X, 5.0f);

        // The brush body's geometry was copied into this document's sidecar.
        EXPECT_NE(host.GetScene().TryGetBrushMesh(body), nullptr);

        // Composition: the light sits at the placement plus its source locals.
        host.GetScene().RefreshDerivedTransforms();
        EXPECT_NEAR(host.GetScene().TryGetWorldTransform(light)->Position.X,
                    5.0f, 1.0e-4f);
        EXPECT_NEAR(host.GetScene().TryGetWorldTransform(light)->Position.Y,
                    3.0f, 1.0e-4f);
    }

    TEST_F(SceneProjectionTest, ProjectedEntitiesStayOutOfTheSavedEntityList)
    {
        EditorDocument host = LoadHost(HostText());
        const std::string saved = host.ToSceneText();
        // One local record; the expanded entities live in the instance record.
        EXPECT_NE(saved.find("00000000000000aa"), std::string::npos);
        EXPECT_EQ(saved.find("Body"), std::string::npos);
        EXPECT_NE(saved.find("instances"), std::string::npos);

        // And the trip is stable: a fresh load of the save saves identically.
        EditorDocument again = LoadHost(saved);
        EXPECT_EQ(again.ToSceneText(), saved);
    }

    TEST_F(SceneProjectionTest, MovingTheRootBecomesThePlacement)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        Transform3f moved = *host.GetScene().TryGetLocalTransform(root);
        moved.Position = Vec3d{ 8.0f, 1.0f, 0.0f };
        host.GetScene().SetTransform(root, moved);

        EditorDocument reloaded = LoadHost(host.ToSceneText());
        const EntityId again = FindById(reloaded, PersistentEntityId{ 0xf0 });
        EXPECT_EQ(reloaded.GetScene().TryGetLocalTransform(again)->Position.X, 8.0f);
    }

    TEST_F(SceneProjectionTest, MemberEditBecomesASparsePatch)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        auto* lamp = host.GetScene().GetRegistry()
                         .Components.TryGet<PointLightComponent>(light);
        ASSERT_NE(lamp, nullptr);
        lamp->Intensity = 40.0f;

        const std::string saved = host.ToSceneText();
        // Sparse: intensity travels, untouched range does not.
        EXPECT_NE(saved.find("intensity"), std::string::npos);
        EXPECT_EQ(saved.find("range"), std::string::npos) << saved;

        EditorDocument reloaded = LoadHost(saved);
        const EntityId again = FindById(reloaded, PersistentEntityId{ 0x202 });
        const auto* applied = reloaded.GetScene().GetRegistry()
                                  .Components.TryGet<PointLightComponent>(again);
        ASSERT_NE(applied, nullptr);
        EXPECT_EQ(applied->Intensity, 40.0f);
        EXPECT_EQ(applied->Range, 3.0f); // the source's value, untouched
    }

    TEST_F(SceneProjectionTest, DeletingAMemberBecomesSuppression)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        host.GetScene().DestroyEntity(light);

        EditorDocument reloaded = LoadHost(host.ToSceneText());
        EXPECT_FALSE(FindById(reloaded, PersistentEntityId{ 0x202 }).IsValid());
        EXPECT_TRUE(FindById(reloaded, PersistentEntityId{ 0x201 }).IsValid());
        EXPECT_TRUE(reloaded.GetProjectionDiagnostics().Clean());

        // The suppression survives a further clean save (the entity is not in
        // the projection any more, and the record must not forget it).
        EditorDocument third = LoadHost(reloaded.ToSceneText());
        EXPECT_FALSE(FindById(third, PersistentEntityId{ 0x202 }).IsValid());
    }

    TEST_F(SceneProjectionTest, AnEntityAddedBeneathAMemberBecomesARecord)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId body = FindById(host, PersistentEntityId{ 0x201 });
        const EntityId trigger = host.GetScene().CreateEntity(Vec3d{ 0, 0, 1 });
        ASSERT_TRUE(host.GetScene().SetParent(trigger, body));
        const PersistentEntityId triggerId = IdOf(host, trigger);

        const std::string saved = host.ToSceneText();
        EXPECT_NE(saved.find("add_entities"), std::string::npos);

        EditorDocument reloaded = LoadHost(saved);
        const EntityId again = FindById(reloaded, triggerId);
        ASSERT_TRUE(again.IsValid());
        EXPECT_EQ(reloaded.GetScene().GetParent(again),
                  FindById(reloaded, PersistentEntityId{ 0x201 }));

        // And it keeps its record across another round trip.
        EditorDocument third = LoadHost(reloaded.ToSceneText());
        EXPECT_TRUE(FindById(third, triggerId).IsValid());
    }

    TEST_F(SceneProjectionTest, DeletingTheRootDeletesThePlacement)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        host.GetScene().DestroySubtree(root);

        const std::string saved = host.ToSceneText();
        EXPECT_EQ(saved.find("instances"), std::string::npos) << saved;

        EditorDocument reloaded = LoadHost(saved);
        EXPECT_EQ(reloaded.GetScene().GetEntityCount(), 1u); // the local alone
    }

    TEST_F(SceneProjectionTest, ASourceThatGrewReportsAndMintsMissingIds)
    {
        // The door gains a handle after the placement was recorded.
        {
            EditorDocument door(Logging);
            door.SetContentRoots({ Root });
            ASSERT_TRUE(door.Load((Root / "props/door.sscene").generic_string()));
            const EntityId handle = door.GetScene().CreateEntity(Vec3d{ 1, 1, 0 });
            ASSERT_TRUE(door.GetScene().SetParent(
                handle, FindById(door, BodyId)));
            ASSERT_TRUE(door.Save());
        }

        EditorDocument host(Logging);
        host.SetContentRoots({ Root });
        ASSERT_TRUE(host.LoadFromSceneText(HostText()));
        ASSERT_EQ(host.GetProjectionDiagnostics().MissingIds.size(), 1u);
        EXPECT_FALSE(host.IsDirty()) << "a load must not mint";

        // The cook refuses the unresolved projection outright.
        std::string cookError;
        EXPECT_FALSE(CollectDocumentCookInput(host, Root, 16.0, Logging, nullptr,
                                              {}, {}, {}, &cookError)
                         .has_value());
        EXPECT_NE(cookError.find("missing id"), std::string::npos) << cookError;

        // Minting is the authoring act that resolves it.
        host.MintMissingInstanceIds();
        EXPECT_TRUE(host.GetProjectionDiagnostics().Clean());
        EXPECT_TRUE(host.IsDirty());
        EXPECT_EQ(host.GetScene().GetEntityCount(), 5u); // handle projected too

        const std::string minted = host.ToSceneText();
        EditorDocument reloaded = LoadHost(minted);
        EXPECT_TRUE(reloaded.GetProjectionDiagnostics().Clean());
        EXPECT_EQ(reloaded.GetScene().GetEntityCount(), 5u) << minted;
    }
} // namespace

#include "document/commands/ReparentEntitiesCommand.h"
#include "document/commands/SceneInstanceCommands.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

namespace
{
    class SceneInstanceCommandTest : public SceneProjectionTest
    {
    protected:
        SelectionContext Context;
        SelectionService Selection{ Context };
    };

    TEST_F(SceneInstanceCommandTest, PlacementMintsOnceAndHoldsIdsAcrossUndoRedo)
    {
        EditorDocument host(Logging);
        host.SetContentRoots({ Root });

        Transform3f placement = Transform3f::Identity();
        placement.Position = Vec3d{ 4.0f, 0.0f, 0.0f };
        PlaceSceneInstanceCommand command("asset://props/door.sscene", placement,
                                          host, Selection);
        command.Execute();
        ASSERT_TRUE(command.Placed());
        ASSERT_EQ(host.GetScene().GetEntityCount(), 3u); // root + body + light
        EXPECT_TRUE(host.IsDirty());

        // The root landed selected, and identity is recorded, not re-derived.
        const SelectableRef primary = Selection.GetPrimarySelection();
        ASSERT_TRUE(primary.IsValid());
        const PersistentEntityId rootId = IdOf(host, primary.Entity);
        EXPECT_TRUE(host.IsSceneInstanceRoot(primary.Entity));

        command.Undo();
        EXPECT_EQ(host.GetScene().GetEntityCount(), 0u);

        command.Execute(); // redo
        ASSERT_EQ(host.GetScene().GetEntityCount(), 3u);
        EXPECT_TRUE(FindById(host, rootId).IsValid())
            << "redo must restore the same minted identity";

        // The round trip carries the placement.
        EditorDocument reloaded(Logging);
        reloaded.SetContentRoots({ Root });
        ASSERT_TRUE(reloaded.LoadFromSceneText(host.ToSceneText()));
        EXPECT_TRUE(FindById(reloaded, rootId).IsValid());
        EXPECT_EQ(reloaded.GetScene().GetEntityCount(), 3u);
    }

    TEST_F(SceneInstanceCommandTest, BreakSeversTheLinkAndUndoRestoresIt)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        auto* lamp = host.GetScene().GetRegistry()
                         .Components.TryGet<PointLightComponent>(light);
        ASSERT_NE(lamp, nullptr);
        lamp->Intensity = 40.0f; // an override the break must not lose

        BreakSceneInstanceCommand command(SceneInstanceId{ 0xf0 }, host);
        command.Execute();

        // Severed: same entities, no link, and the save writes them as locals.
        ASSERT_EQ(host.GetScene().GetEntityCount(), 4u);
        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        EXPECT_FALSE(host.IsSceneInstanceRoot(root));
        EXPECT_FALSE(host.IsSceneInstanceMember(
            FindById(host, PersistentEntityId{ 0x201 })));
        const std::string severed = host.ToSceneText();
        EXPECT_EQ(severed.find("instances"), std::string::npos);
        EXPECT_NE(severed.find("PointLight"), std::string::npos);

        command.Undo();
        ASSERT_EQ(host.GetScene().GetEntityCount(), 4u);
        EXPECT_TRUE(host.IsSceneInstanceRoot(
            FindById(host, PersistentEntityId{ 0xf0 })));
        // The edit made before the break came back as the override it was.
        const auto* restored = host.GetScene().GetRegistry()
                                   .Components.TryGet<PointLightComponent>(
                                       FindById(host, PersistentEntityId{ 0x202 }));
        ASSERT_NE(restored, nullptr);
        EXPECT_EQ(restored->Intensity, 40.0f);
    }

    TEST_F(SceneInstanceCommandTest, ReparentRefusesMembersAndAllowsTheRoot)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId local = FindById(host, PersistentEntityId{ 0xaa });
        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        const EntityId body = FindById(host, PersistentEntityId{ 0x201 });

        const std::array member{ body };
        EXPECT_EQ(MakeReparentEntitiesCommand(member, local,
                      ReparentTransformRule::KeepWorld, host.GetScene(), host),
                  nullptr);

        // The root is the placement and moves freely; unparenting it round
        // trips through the record's parent field.
        const std::array roots{ root };
        auto command = MakeReparentEntitiesCommand(roots, EntityId{},
            ReparentTransformRule::KeepWorld, host.GetScene(), host);
        ASSERT_NE(command, nullptr);
        command->Execute();

        EditorDocument reloaded = LoadHost(host.ToSceneText());
        EXPECT_EQ(reloaded.GetScene().GetParent(
                      FindById(reloaded, PersistentEntityId{ 0xf0 })),
                  EntityId{});
    }
} // namespace
