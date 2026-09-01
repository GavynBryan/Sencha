// What the template's authored archetypes actually contain, read from the
// checkout rather than restated here.
//
// A prefab that loses a component says nothing: the entity is built, it is just
// missing the piece that made it a pawn, and the failure surfaces frames later
// as something that will not move. Every component below is one the code used
// to add by hand, so a silent drop here is a silent return to that.

#include "document/DocumentCook.h"
#include "document/EditorDocument.h"
#include "document/DocumentSerialization.h"

#include "TemplateComponents.h"

#include <assets/runtime/RuntimeAssets.h>
#include <world/transform/TransformComponents.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <abilities/AbilitySet.h>
#include <camera/CameraSeat.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponentSchemas.h>
#include <movement/MovementIntent.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformHistory.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
    class ShippedPrefabTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            RegisterDocumentSerializers();
            ComponentRegistrar registrar(nullptr, &EditorSceneSerializers(), nullptr);
            RegisterTemplateComponents(registrar);
        }

        void SetUp() override
        {
            Root = std::filesystem::path(SENCHA_REPO_ROOT) / "template/assets";
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry,
                                      Assets.Assets.Kinds());
            Document.SetAssetEnvironment(Assets);
            Document.SetContentRoots({ Root });
        }

        [[nodiscard]] bool LoadPrefab(const std::string& relative)
        {
            return Document.Load((Root / relative).generic_string());
        }

        // The prefab's root: the one entity nothing parents. A prefab with
        // more than one has no single thing to bind an identity to, which the
        // net spawner refuses outright.
        [[nodiscard]] EntityId RootEntity() const
        {
            const World& world = Document.GetRegistry().Components;
            EntityId found{};
            std::size_t count = 0;
            world.ForEachComponent<LocalTransform>(
                [&](EntityId entity, const LocalTransform&)
                {
                    if (world.TryGet<Parent>(entity) != nullptr)
                        return;
                    found = entity;
                    ++count;
                });
            EXPECT_EQ(count, 1u) << "a prefab under test has more than one root";
            return found;
        }

        // A child of the root carrying T, or an invalid id.
        template <typename T>
        [[nodiscard]] EntityId ChildWith(EntityId root) const
        {
            const World& world = Document.GetRegistry().Components;
            if (!world.IsRegistered<T>())
                return EntityId{};
            for (const EntityId entity : world.GetAliveEntities())
            {
                const Parent* parent = world.TryGet<Parent>(entity);
                if (parent != nullptr && parent->Entity == root
                    && world.HasComponent<T>(entity))
                {
                    return entity;
                }
            }
            return EntityId{};
        }

        // A component this document's vocabulary never had reads as absent
        // rather than asserting, which is what a runtime-only column is here.
        template <typename T>
        [[nodiscard]] const T* Get(EntityId entity) const
        {
            const World& world = Document.GetRegistry().Components;
            return world.IsRegistered<T>() ? world.TryGet<T>(entity) : nullptr;
        }

        // A tag has no column, so TryGet answers null for one that is present.
        template <typename T>
        [[nodiscard]] bool Carries(EntityId entity) const
        {
            const World& world = Document.GetRegistry().Components;
            return world.IsRegistered<T>() && world.HasComponent<T>(entity);
        }

        std::filesystem::path Root;
        LoggingProvider Logging;
        RuntimeAssets Assets{ Logging, EditorSceneSerializers() };
        EditorDocument Document{ Logging };
    };
}

TEST_F(ShippedPrefabTest, ThePawnCarriesWhatMakesItAPawn)
{
    ASSERT_TRUE(LoadPrefab("prefabs/player_pawn.sscene"));
    const EntityId pawn = RootEntity();
    ASSERT_TRUE(pawn.IsValid());

    // A body that collides, moves, aims, and turns to its aim.
    EXPECT_NE(Get<CharacterController>(pawn), nullptr);
    EXPECT_NE(Get<CharacterMovement>(pawn), nullptr);
    EXPECT_NE(Get<LookOrientation>(pawn), nullptr);
    EXPECT_TRUE(Carries<AimFacing>(pawn));

    // The authored tuning, resolved rather than merely named: an unresolved
    // profile is a pawn silently moving on engine defaults.
    const MovementTuningSource* tuning = Get<MovementTuningSource>(pawn);
    ASSERT_NE(tuning, nullptr);
    EXPECT_TRUE(tuning->Profile.IsValid())
        << "the pawn's movement profile did not resolve";

    // The free mode by name, not by whatever id this process handed out.
    const LocomotionModeRegistry* modes =
        Document.GetRegistry().Components.TryGetResource<LocomotionModeRegistry>();
    ASSERT_NE(modes, nullptr);
    EXPECT_EQ(Get<CharacterMovement>(pawn)->Mode, modes->FreeMode());

    // What movement systems select on, and the speed effects modify.
    const GameplayTagRegistry* tags =
        Document.GetRegistry().Components.TryGetResource<GameplayTagRegistry>();
    ASSERT_NE(tags, nullptr);
    const GameplayTagContainer* pawnTags = Get<GameplayTagContainer>(pawn);
    ASSERT_NE(pawnTags, nullptr);
    EXPECT_TRUE(pawnTags->HasExact(tags->FindTag("movement.controlled")));

    const AttributeRegistry* attributes =
        Document.GetRegistry().Components.TryGetResource<AttributeRegistry>();
    ASSERT_NE(attributes, nullptr);
    const AttributeSet* pawnAttributes = Get<AttributeSet>(pawn);
    ASSERT_NE(pawnAttributes, nullptr);
    EXPECT_FLOAT_EQ(pawnAttributes->GetBase(attributes->FindAttribute("MoveSpeed")), 4.5f);

    EXPECT_NE(Get<AbilitySet>(pawn), nullptr);

    // And the per-tick columns the movement step reads, which content does not
    // author and never should: the movement component owes them, so a pawn
    // built from this file alone is one the systems can already move.
    EXPECT_NE(Get<MovementIntent>(pawn), nullptr);
    EXPECT_NE(Get<ResolvedMovementTuning>(pawn), nullptr);
    EXPECT_NE(Get<LocomotionOutput>(pawn), nullptr);
    EXPECT_NE(Get<MotionRequest>(pawn), nullptr);
    EXPECT_NE(Get<KinematicState>(pawn), nullptr);
    EXPECT_NE(Get<SupportState>(pawn), nullptr);
    // Including the pose history, which a document used to skip because it
    // registered a smaller vocabulary than the runtime and the closure only
    // provisions what the world knows. A document that builds a smaller entity
    // than the game does from the same file is a document that cannot be
    // trusted to show what it is authoring.
    EXPECT_NE(Get<WorldTransformHistory>(pawn), nullptr);

    // And the camera it is watched from, which the prefab places and names.
    // Possession takes the seat rather than the first camera it finds, so a
    // pawn that lost this would silently be watched from somewhere else.
    const EntityId seatEntity = ChildWith<CameraSeat>(pawn);
    ASSERT_TRUE(seatEntity.IsValid())
        << "the pawn prefab carries no camera seat";
    const CameraSeat* seat = Get<CameraSeat>(seatEntity);
    ASSERT_NE(seat, nullptr);
    EXPECT_EQ(seat->Role, CameraSeatRole::Primary);
    EXPECT_EQ(seat->Mode, CameraRigMode::FirstPerson)
        << "this template's player is first person; a third-person game is the "
           "same pawn with a different seat";
    EXPECT_NE(Get<CameraComponent>(seatEntity), nullptr)
        << "a seat with no camera on it is a seat nothing can look through";
}

TEST_F(ShippedPrefabTest, TheTurretAimsAndTurns)
{
    ASSERT_TRUE(LoadPrefab("prefabs/turret.sscene"));
    const EntityId turret = RootEntity();
    ASSERT_TRUE(turret.IsValid());

    EXPECT_NE(Get<LookOrientation>(turret), nullptr);
    EXPECT_TRUE(Carries<AimFacing>(turret));
    // A turret does not move, so it has neither a controller nor tuning.
    EXPECT_EQ(Get<CharacterController>(turret), nullptr);
    EXPECT_EQ(Get<MovementTuningSource>(turret), nullptr);
}

// The save side of the same contract: what the document read back it can write
// back, so a round trip through the editor does not quietly empty the prefab.
TEST_F(ShippedPrefabTest, ThePawnSurvivesADocumentRoundTrip)
{
    ASSERT_TRUE(LoadPrefab("prefabs/player_pawn.sscene"));
    const std::string text = Document.ToSceneText();
    ASSERT_FALSE(text.empty());

    EditorDocument reloaded(Logging);
    reloaded.SetAssetEnvironment(Assets);
    reloaded.SetContentRoots({ Root });
    ASSERT_TRUE(reloaded.LoadFromSceneText(text));

    EntityId pawn{};
    const World& reloadedWorld = reloaded.GetRegistry().Components;
    reloadedWorld.ForEachComponent<LocalTransform>(
        [&](EntityId entity, const LocalTransform&)
        {
            if (reloadedWorld.TryGet<Parent>(entity) == nullptr)
                pawn = entity;
        });
    ASSERT_TRUE(pawn.IsValid());

    const MovementTuningSource* tuning =
        reloaded.GetRegistry().Components.TryGet<MovementTuningSource>(pawn);
    ASSERT_NE(tuning, nullptr);
    EXPECT_TRUE(tuning->Profile.IsValid())
        << "the saved document named no profile, so the round trip lost it";
    EXPECT_NE(reloaded.GetRegistry().Components.TryGet<GameplayTagContainer>(pawn),
              nullptr);
    EXPECT_NE(reloaded.GetRegistry().Components.TryGet<AttributeSet>(pawn), nullptr);
    EXPECT_TRUE(reloaded.GetRegistry().Components.HasComponent<AimFacing>(pawn));
}
