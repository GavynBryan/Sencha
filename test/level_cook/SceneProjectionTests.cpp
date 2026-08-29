// The instance projection: placement records expand into derived live
// entities on load, live edits fold back into the records on save, and the
// two directions agree -- what a fresh document loads from a saved text is
// what the editing session meant.

#include "document/DocumentCook.h"
#include "CookedSmapReaders.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
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
  entities: [ { id: '00000000000000aa',
      components: { Transform: { local: { position: [0, 0, 0],
                                          rotation: [0, 0, 0, 1],
                                          scale: [1, 1, 1] } } } } ],
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

    // A saved override has to survive a session that merely opens the document
    // and saves it again. The harvest rebuilds a projected path's records from
    // the live entities, so an override the load applied must still be read
    // back out of them rather than dropped as "nothing changed this session".
    TEST_F(SceneProjectionTest, ASavedOverrideSurvivesAnEditlessRoundTrip)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        auto* lamp = host.GetScene().GetRegistry()
                         .Components.TryGet<PointLightComponent>(light);
        ASSERT_NE(lamp, nullptr);
        lamp->Intensity = 40.0f;
        const std::string first = host.ToSceneText();
        ASSERT_NE(first.find("intensity"), std::string::npos);

        // Open it again and save without touching anything.
        EditorDocument reopened = LoadHost(first);
        const std::string second = reopened.ToSceneText();
        EXPECT_NE(second.find("intensity"), std::string::npos)
            << "the override was dropped by an editless save\n" << second;
        EXPECT_EQ(second, first);

        // And the value is still applied after that second trip.
        EditorDocument third = LoadHost(second);
        const auto* applied = third.GetScene().GetRegistry()
                                  .Components.TryGet<PointLightComponent>(
                                      FindById(third, PersistentEntityId{ 0x202 }));
        ASSERT_NE(applied, nullptr);
        EXPECT_EQ(applied->Intensity, 40.0f);
    }

    // Because the baseline is what the SOURCE says, putting a value back to it
    // leaves nothing to record. That is what makes an override resettable by
    // editing, and what keeps a record from accreting no-op patches.
    TEST_F(SceneProjectionTest, EditingAValueBackToItsSourceClearsTheOverride)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId light = FindById(host, PersistentEntityId{ 0x202 });
        auto* lamp = host.GetScene().GetRegistry()
                         .Components.TryGet<PointLightComponent>(light);
        ASSERT_NE(lamp, nullptr);
        const float sourceIntensity = lamp->Intensity;
        lamp->Intensity = 40.0f;
        ASSERT_NE(host.ToSceneText().find("intensity"), std::string::npos);

        // Reopen so the override arrives from the record rather than the live
        // edit, then put the value back where the source had it.
        EditorDocument reopened = LoadHost(host.ToSceneText());
        auto* reloaded = reopened.GetScene().GetRegistry()
                             .Components.TryGet<PointLightComponent>(
                                 FindById(reopened, PersistentEntityId{ 0x202 }));
        ASSERT_NE(reloaded, nullptr);
        ASSERT_EQ(reloaded->Intensity, 40.0f);
        reloaded->Intensity = sourceIntensity;

        const std::string saved = reopened.ToSceneText();
        EXPECT_EQ(saved.find("intensity"), std::string::npos)
            << "a value back at its source is not an override\n" << saved;
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

    // A projection rebuild destroys every expanded entity and recreates it, so
    // a selection holding raw handles goes dead under the user. Identity is
    // what survives; the handle is re-resolved from it.
    TEST_F(SceneInstanceCommandTest, SelectionFollowsEntitiesThroughARebuild)
    {
        EditorDocument host(Logging);
        host.SetContentRoots({ Root });
        // The workspace's wiring, reproduced: identity source plus the
        // rebuild announcement that drives the retarget.
        Selection.BindDocument(&host.GetScene().GetRegistry().Components);
        host.SetProjectionObserver([this] { Selection.RetargetToDocument(); });

        PlaceSceneInstanceCommand place("asset://props/door.sscene",
                                        Transform3f::Identity(), host, Selection);
        place.Execute();
        ASSERT_TRUE(place.Placed());

        // Select the light the placement contributed: a projected member, not
        // the root, so nothing re-selects it on our behalf.
        EntityId light;
        for (EntityId entity : host.GetScene().GetAllEntities())
            if (host.GetRegistry().Components.TryGet<PointLightComponent>(entity)
                != nullptr)
                light = entity;
        ASSERT_TRUE(light.IsValid());
        const PersistentEntityId lightId = IdOf(host, light);

        Selection.ApplySelection(SelectableRef::EntitySelection(
            host.GetScene().GetRegistry().Id, light));
        ASSERT_EQ(Selection.GetPrimarySelection().Entity, light);

        host.RebuildSceneProjection();

        // Guard against a vacuous test: the rebuild must really have recreated
        // the entity under a different handle, or there was nothing to survive.
        const EntityId rebuilt = FindById(host, lightId);
        ASSERT_TRUE(rebuilt.IsValid());
        ASSERT_NE(rebuilt, light) << "the rebuild did not recreate the entity";

        const SelectableRef primary = Selection.GetPrimarySelection();
        EXPECT_TRUE(primary.IsValid());
        EXPECT_EQ(primary.Entity, rebuilt)
            << "the selection must follow the entity to its new handle";
        EXPECT_EQ(primary.Stable, lightId);
        ASSERT_EQ(Selection.GetSelection().size(), 1u);
        EXPECT_EQ(Selection.GetSelection()[0].Entity, rebuilt);
    }

    // The other half of the contract: an entity that is genuinely gone leaves
    // the selection rather than lingering as an unresolvable ref.
    TEST_F(SceneInstanceCommandTest, SelectionDropsWhatTheRebuildRemoved)
    {
        EditorDocument host(Logging);
        host.SetContentRoots({ Root });
        Selection.BindDocument(&host.GetScene().GetRegistry().Components);
        host.SetProjectionObserver([this] { Selection.RetargetToDocument(); });

        PlaceSceneInstanceCommand place("asset://props/door.sscene",
                                        Transform3f::Identity(), host, Selection);
        place.Execute();
        ASSERT_TRUE(place.Placed());

        EntityId light;
        for (EntityId entity : host.GetScene().GetAllEntities())
            if (host.GetRegistry().Components.TryGet<PointLightComponent>(entity)
                != nullptr)
                light = entity;
        ASSERT_TRUE(light.IsValid());
        Selection.ApplySelection(SelectableRef::EntitySelection(
            host.GetScene().GetRegistry().Id, light));
        ASSERT_FALSE(Selection.GetSelection().empty());

        const SceneInstanceId owner = host.SceneInstanceOwnerOf(light);
        ASSERT_TRUE(owner.IsValid());
        ASSERT_TRUE(host.RemoveSceneInstance(owner));

        EXPECT_TRUE(Selection.GetSelection().empty())
            << "a removed entity must not stay selected";
        EXPECT_FALSE(Selection.GetPrimarySelection().IsValid());
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

namespace
{
    TEST_F(SceneInstanceCommandTest, APlacedInstanceCooksIntoTheRuntimeScene)
    {
        // Author the host on disk: one placement, saved through the document.
        EditorDocument host(Logging);
        host.SetContentRoots({ Root });
        Transform3f placement = Transform3f::Identity();
        placement.Position = Vec3d{ 5.0f, 0.0f, 0.0f };
        PlaceSceneInstanceCommand place("asset://props/door.sscene", placement,
                                        host, Selection);
        place.Execute();
        ASSERT_TRUE(place.Placed());
        fs::create_directories(Root / "levels");
        fs::create_directories(Root / "materials/dev");
        std::ofstream(Root / "materials/dev/gray.smat", std::ios::trunc) << "{}";
        ASSERT_TRUE(host.SaveAs((Root / "levels/host.sscene").generic_string()));

        // The cook loads the same projecting document, so the expanded
        // entities -- authored identity included -- are simply part of the
        // scene it bakes.
        const DocumentCookResult cooked =
            CookDocument(Root / "levels/host.sscene", Root, 16.0);
        ASSERT_TRUE(cooked.Success) << cooked.Error;

        const SmapContents scene = ReadCookedScene(cooked.CookedScenePath);

        // The light the door contributes, under its minted id, at rest in the
        // cooked output the runtime will stream.
        std::vector<PersistentEntityId> cookedLightIds;
        for (const SmapEntityRecord& record : scene.Entities)
            if (FindCookedComponent(record, "PointLight") != nullptr)
                cookedLightIds.push_back(record.Persistent);
        EXPECT_FALSE(cookedLightIds.empty());

        // D1: every expanded member carries its placement's identity into the
        // cooked scene -- one scene_instance id across the group, the source
        // named (as the stamped ref or the bare path, per the id map).
        std::vector<std::string> groupIds;
        for (const SmapEntityRecord& record : scene.Entities)
            if (const JsonValue* payload =
                    FindCookedComponent(record, "scene_instance"))
            {
                ASSERT_TRUE(payload->IsObject());
                const JsonValue* instanceId = payload->Find("id");
                ASSERT_NE(instanceId, nullptr);
                ASSERT_TRUE(instanceId->IsString());
                groupIds.push_back(instanceId->AsString());
                ASSERT_NE(payload->Find("source"), nullptr);
            }
        // Root + light. The brush body bakes into cell geometry rather than
        // passing through, so it carries no membership.
        ASSERT_GE(groupIds.size(), 2u);
        for (const std::string& id : groupIds)
            EXPECT_EQ(id, groupIds.front());

        const EditorDocument reloaded = [&]
        {
            EditorDocument doc(Logging);
            doc.SetContentRoots({ Root });
            EXPECT_TRUE(doc.Load((Root / "levels/host.sscene").generic_string()));
            return doc;
        }();
        for (EntityId entity : reloaded.GetScene().GetAllEntities())
            if (reloaded.IsSceneInstanceMember(entity)
                && reloaded.GetRegistry().Components
                       .TryGet<PointLightComponent>(entity) != nullptr)
            {
                const PersistentEntityId id = IdOf(reloaded, entity);
                EXPECT_NE(std::find(cookedLightIds.begin(), cookedLightIds.end(),
                                    id),
                          cookedLightIds.end())
                    << "cooked identity must be the minted id";
            }
    }
} // namespace

#include "document/commands/DeleteEntityCommand.h"

namespace
{
    TEST_F(SceneInstanceCommandTest, PlaceDeletePlaceAgainDoesNotCrash)
    {
        // A second source, so the second drag is a different scene.
        {
            EditorDocument lamp(Logging);
            const EntityId bulb = lamp.GetScene().CreateEntity(Vec3d{ 0, 1, 0 });
            PointLightComponent light{};
            light.Intensity = 2.0f;
            lamp.GetScene().GetRegistry().Components.AddComponent(bulb, light);
            ASSERT_TRUE(lamp.SaveAs((Root / "props/lamp.sscene").generic_string()));
        }

        EditorDocument host(Logging);
        host.SetContentRoots({ Root });

        auto place = std::make_unique<PlaceSceneInstanceCommand>(
            "asset://props/door.sscene", Transform3f::Identity(), host, Selection);
        place->Execute();
        ASSERT_TRUE(place->Placed());

        // Delete the placement the way the hierarchy panel does: the selected
        // root expands to its subtree.
        const SelectableRef primary = Selection.GetPrimarySelection();
        ASSERT_TRUE(primary.IsValid());
        const EntityId root = primary.Entity;
        auto erase = MakeDeleteEntitiesCommand(
            std::span<const EntityId>(&root, 1), host.GetScene(), host, Selection);
        erase->Execute();
        EXPECT_EQ(host.GetScene().GetEntityCount(), 0u);

        // The second drag.
        auto placeAgain = std::make_unique<PlaceSceneInstanceCommand>(
            "asset://props/lamp.sscene", Transform3f::Identity(), host, Selection);
        placeAgain->Execute();
        ASSERT_TRUE(placeAgain->Placed());
        EXPECT_EQ(host.GetScene().GetEntityCount(), 2u); // lamp root + bulb

        // And a same-source double placement, the other half of the report.
        auto placeDoor = std::make_unique<PlaceSceneInstanceCommand>(
            "asset://props/door.sscene", Transform3f::Identity(), host, Selection);
        placeDoor->Execute();
        ASSERT_TRUE(placeDoor->Placed());
        EXPECT_EQ(host.GetScene().GetEntityCount(), 5u);
        auto placeDoorTwice = std::make_unique<PlaceSceneInstanceCommand>(
            "asset://props/door.sscene", Transform3f::Identity(), host, Selection);
        placeDoorTwice->Execute();
        ASSERT_TRUE(placeDoorTwice->Placed());
        EXPECT_EQ(host.GetScene().GetEntityCount(), 8u);
    }
} // namespace

#include "document/commands/RenameEntityCommand.h"
#include "document/commands/SetSceneOriginCommand.h"

namespace
{
    TEST_F(SceneInstanceCommandTest, RenamingThePlacementRootPersistsInTheRecord)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId root = FindById(host, PersistentEntityId{ 0xf0 });
        auto rename = MakeRenameEntityCommand(root, "Front Door",
                                              host.GetScene(), host);
        ASSERT_NE(rename, nullptr);
        rename->Execute();

        const std::string saved = host.ToSceneText();
        EXPECT_NE(saved.find("name: \"Front Door\""), std::string::npos) << saved;

        EditorDocument reloaded = LoadHost(saved);
        const EntityId again = FindById(reloaded, PersistentEntityId{ 0xf0 });
        const auto* name = reloaded.GetRegistry()
                               .Components.TryGet<EntityNameComponent>(again);
        ASSERT_NE(name, nullptr);
        EXPECT_EQ(std::string(name->Value.View()), "Front Door");
    }

    TEST_F(SceneInstanceCommandTest, SetSceneOriginShiftsRootsAndUndoRestores)
    {
        EditorDocument host = LoadHost(HostText());
        const EntityId local = FindById(host, PersistentEntityId{ 0xaa });
        const EntityId body = FindById(host, PersistentEntityId{ 0x201 });
        host.GetScene().RefreshDerivedTransforms();
        const Vec3d bodyBefore =
            host.GetScene().ComposeWorldTransform(body).Position;

        // The body should sit at the origin afterwards; everything shifts by
        // its world position, children following their roots.
        auto command = MakeSetSceneOriginCommand(host.GetScene(), host, bodyBefore);
        ASSERT_NE(command, nullptr);
        command->Execute();
        host.GetScene().RefreshDerivedTransforms();

        const Vec3d bodyAfter =
            host.GetScene().ComposeWorldTransform(body).Position;
        EXPECT_NEAR(bodyAfter.X, 0.0f, 1.0e-4f);
        EXPECT_NEAR(bodyAfter.Y, 0.0f, 1.0e-4f);
        EXPECT_NEAR(bodyAfter.Z, 0.0f, 1.0e-4f);
        // The local root moved by the same shift.
        EXPECT_NEAR(host.GetScene().ComposeWorldTransform(local).Position.X,
                    -bodyBefore.X, 1.0e-4f);

        command->Undo();
        host.GetScene().RefreshDerivedTransforms();
        EXPECT_NEAR(host.GetScene().ComposeWorldTransform(body).Position.X,
                    bodyBefore.X, 1.0e-4f);
    }
} // namespace

#include "document/BrushCookInput.h"

namespace
{
    TEST_F(SceneInstanceCommandTest, AProjectedBrushBakesLikeAnAuthoredOne)
    {
        // The same door, authored directly and arrived through a placement.
        EditorDocument direct(Logging);
        direct.SetDefaultMaterial(AssetRef{ AssetType::Material,
                                            "asset://materials/dev/gray.smat" });
        (void)direct.GetScene().CreateBrush(Vec3d{ 5.0f, 1.0f, 0.0f });
        direct.GetScene().RefreshDerivedTransforms();
        const std::vector<CookBrushGeometry> authored =
            CollectCookBrushes(direct.GetScene(), direct.GetDefaultMaterial());

        EditorDocument host = LoadHost(HostText());
        host.SetDefaultMaterial(AssetRef{ AssetType::Material,
                                          "asset://materials/dev/gray.smat" });
        host.GetScene().RefreshDerivedTransforms();
        const std::vector<CookBrushGeometry> projected =
            CollectCookBrushes(host.GetScene(), host.GetDefaultMaterial());

        ASSERT_EQ(authored.size(), 1u);
        ASSERT_EQ(projected.size(), 1u);
        ASSERT_EQ(projected[0].Faces.size(), authored[0].Faces.size());

        for (std::size_t f = 0; f < projected[0].Faces.size(); ++f)
        {
            const CookFace& copy = projected[0].Faces[f];
            const CookFace& original = authored[0].Faces[f];
            EXPECT_EQ(copy.Material.Path, original.Material.Path)
                << "face " << f << " material diverged";
            EXPECT_FALSE(copy.Material.Path.empty());
            ASSERT_EQ(copy.Triangles.size(), original.Triangles.size());
            for (std::size_t v = 0; v < copy.Triangles.size(); ++v)
            {
                const Vec3d n = copy.Triangles[v].Normal;
                EXPECT_NEAR(n.Magnitude(), 1.0f, 1.0e-3f)
                    << "face " << f << " vertex " << v << " normal degenerate: ("
                    << n.X << ", " << n.Y << ", " << n.Z << ")";
                const Vec3d expected = original.Triangles[v].Normal;
                EXPECT_NEAR(n.Dot(expected), 1.0f, 1.0e-3f)
                    << "face " << f << " vertex " << v << " normal diverged";
            }
        }
    }
} // namespace
