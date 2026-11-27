// The vocabulary catalog lists what a document can name, a game module's names
// beside the engine's, and whether each binding-asset record resolves, all
// without a dispatcher.

#include <app/ShellVerbs.h>
#include "vocabulary/VocabularyCatalog.h"

#include <app/LevelChangeVocabulary.h>
#include <save/SavedGameVocabulary.h>

#include <algorithm>
#include <line/ContactLineSystem.h>
#include <abilities/AbilityVerbs.h>
#include <gameplay_tags/GameplayTagVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <app/EngineVocabulary.h>
#include <attach/AttachmentVerbs.h>
#include <audio/AudioVerbs.h>
#include <camera/CameraFovScaleVerbs.h>
#include <camera/CameraShakeVerbs.h>
#include <app/Game.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <effects/EffectVerbs.h>
#include <logic/ClaimVocabulary.h>
#include <equipment/EquipmentVocabulary.h>
#include <inventory/InventoryVocabulary.h>
#include <logic/CounterVocabulary.h>
#include <logic/InteractUse.h>
#include <logic/ResponseVerbs.h>
#include <projectile/ProjectileHit.h>
#include <logic/ResponsesSystem.h>
#include <movement/MovementVerbs.h>
#include <navigation/NavigationVerbs.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <time/ActorTimeVerbs.h>

#include <gtest/gtest.h>

namespace
{
constexpr const char* kModuleVerb = "spike.grapple.fire";

// What a module's hook does, and nothing else: names into the World it is
// handed. It never sees an engine.
class SpikeGame final : public Game
{
public:
    void OnRegisterVocabulary(World& world) override
    {
        ++Calls;
        VerbRegistry* verbs = FindVerbRegistry(world);
        if (verbs == nullptr)
            return;
        VerbRegistrationScope scope(*verbs, "spike");
        VerbDefinition fire;
        fire.Name = kModuleVerb;
        fire.DisplayName = "Fire grapple";
        DataFieldSchema range;
        range.Key = "Range";
        range.Kind = DataFieldKind::Float;
        fire.Arguments.Children.push_back(std::move(range));
        (void)scope.Declare(std::move(fire));
        if (DeclareBadly)
        {
            VerbDefinition bad;
            bad.Name = "spike..broken";
            (void)scope.Declare(std::move(bad));
        }
        (void)scope.Commit();
    }

    int Calls = 0;
    bool DeclareBadly = false;
};

[[nodiscard]] VerbBindingDesc Record(std::string key, std::string verb)
{
    VerbBindingDesc desc;
    desc.Key = std::move(key);
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    return desc;
}
}

TEST(VocabularyCatalog, TheEngineVerbsAreThereBeforeAnyModuleIs)
{
    VocabularyCatalog catalog;
    EXPECT_TRUE(catalog.Errors().empty());
    const std::vector<VocabularyCatalog::VerbRow> verbs = catalog.ListVerbs();
    ASSERT_EQ(verbs.size(), 57u);
    EXPECT_EQ(verbs[0].Name, AuthoredApiDefinition<RuntimeResumeOperation>::Name_Resume);
    EXPECT_EQ(verbs[0].Provider, "engine");
    EXPECT_EQ(verbs[1].Name, AuthoredApiDefinition<ApplicationQuitOperation>::Name_Quit);
    EXPECT_EQ(verbs[2].Name, AuthoredApiDefinition<AnimRequestOperations>::Name_Request);
    EXPECT_EQ(verbs[3].Name, AuthoredApiDefinition<AnimRequestOperations>::Name_Cancel);
    EXPECT_EQ(verbs[4].Name, AuthoredApiDefinition<AnimRequestOperations>::Name_Hold);
    EXPECT_EQ(verbs[5].Name, AuthoredApiDefinition<AnimRequestOperations>::Name_Present);
    EXPECT_EQ(verbs[6].Name, AuthoredApiDefinition<EffectOperations>::Name_Apply);
    EXPECT_EQ(verbs[7].Name, AuthoredApiDefinition<EffectOperations>::Name_Remove);
    EXPECT_EQ(verbs[8].Name, AuthoredApiDefinition<EffectOperations>::Name_Hold);
    EXPECT_EQ(verbs[9].Name, AuthoredApiDefinition<AbilityOperations>::Name_Activate);
    EXPECT_EQ(verbs[12].Name, AuthoredApiDefinition<AbilityOperations>::Name_Revoke);
    EXPECT_EQ(verbs[13].Name, AuthoredApiDefinition<GameplayTagOperations>::Name_Grant);
    EXPECT_EQ(verbs[14].Name, AuthoredApiDefinition<GameplayTagOperations>::Name_Revoke);
    EXPECT_EQ(verbs[15].Name, AuthoredApiDefinition<ActorTimeOperations>::Name_Scale);
    EXPECT_EQ(verbs[16].Name, AuthoredApiDefinition<MovementOperations>::Name_Burst);
    EXPECT_EQ(verbs[18].Name, AuthoredApiDefinition<MovementOperations>::Name_Mode);
    EXPECT_EQ(verbs[19].Name, AuthoredApiDefinition<MovementOperations>::Name_Launch);
    EXPECT_EQ(verbs[20].Name, AuthoredApiDefinition<MovementOperations>::Name_Impulse);
    EXPECT_EQ(verbs[21].Name, AuthoredApiDefinition<AudioOperations>::Name_Play);
    EXPECT_EQ(verbs[22].Name, AuthoredApiDefinition<AudioOperations>::Name_Loop);
    EXPECT_EQ(verbs[23].Name, AuthoredApiDefinition<AttachmentOperations>::Name_Attach);
    EXPECT_EQ(verbs[24].Name, AuthoredApiDefinition<AttachedLightOperations>::Name_Light);
    EXPECT_EQ(verbs[25].Name, AuthoredApiDefinition<CameraShakeOperations>::Name_Shake);
    EXPECT_EQ(verbs[26].Name, AuthoredApiDefinition<CameraShakeOperations>::Name_Hold);
    EXPECT_EQ(verbs[27].Name, AuthoredApiDefinition<CameraFovScaleOperations>::Name_Pulse);
    EXPECT_EQ(verbs[28].Name, AuthoredApiDefinition<CameraFovScaleOperations>::Name_Hold);
    EXPECT_EQ(verbs[29].Name, AuthoredApiDefinition<NavigationOperations>::Name_MoveTo);
    EXPECT_EQ(verbs[30].Name, AuthoredApiDefinition<NavigationOperations>::Name_Approach);
    EXPECT_EQ(verbs[31].Name, AuthoredApiDefinition<NavigationOperations>::Name_Stop);
    EXPECT_EQ(verbs[32].Name, AuthoredApiDefinition<ResponseOperations>::Name_Enable);
    EXPECT_EQ(verbs[33].Name, AuthoredApiDefinition<ResponseOperations>::Name_Disable);
    EXPECT_EQ(verbs[34].Name, AuthoredApiDefinition<ResponseOperations>::Name_Interact);
    EXPECT_EQ(verbs[36].Name, AuthoredApiDefinition<ContactLineSystem>::Name_Extend);
    EXPECT_EQ(verbs[37].Name, AuthoredApiDefinition<ContactLineSystem>::Name_Cut);
    EXPECT_EQ(verbs[38].Name, AuthoredApiDefinition<ContactLineSystem>::Name_Retract);
    EXPECT_EQ(verbs[39].Name, AuthoredApiDefinition<InteractUseOperations>::Name_Use);
    EXPECT_EQ(verbs[40].Name, AuthoredApiDefinition<SavedGameOperations>::Name_Write);
    EXPECT_EQ(verbs[41].Name, AuthoredApiDefinition<SavedGameOperations>::Name_Restore);
    EXPECT_EQ(verbs[42].Name, AuthoredApiDefinition<SavedGameOperations>::Name_Delete);
    EXPECT_EQ(verbs[43].Name, AuthoredApiDefinition<SavedGameOperations>::Name_Copy);
    EXPECT_EQ(verbs[44].Name, AuthoredApiDefinition<LevelChangeOperations>::Name_Change);
    EXPECT_EQ(verbs[45].Name, AuthoredApiDefinition<CounterOperations>::Name_Set);
    EXPECT_EQ(verbs[46].Name, AuthoredApiDefinition<CounterOperations>::Name_Add);
    EXPECT_EQ(verbs[47].Name, AuthoredApiDefinition<InventoryOperations>::Name_Take);
    EXPECT_EQ(verbs[48].Name, AuthoredApiDefinition<InventoryOperations>::Name_Give);
    EXPECT_EQ(verbs[49].Name, AuthoredApiDefinition<InventoryOperations>::Name_Transfer);
    EXPECT_EQ(verbs[50].Name, AuthoredApiDefinition<InventoryOperations>::Name_Drop);
    EXPECT_EQ(verbs[51].Name, AuthoredApiDefinition<InventoryOperations>::Name_Use);
    EXPECT_EQ(verbs[52].Name, AuthoredApiDefinition<EquipmentOperations>::Name_Equip);
    EXPECT_EQ(verbs[54].Name, AuthoredApiDefinition<EquipmentOperations>::Name_Cycle);
    EXPECT_EQ(verbs[55].Name, AuthoredApiDefinition<PerceptionOperations>::Name_Stimulus);
    EXPECT_EQ(verbs[56].Name, AuthoredApiDefinition<ClaimOperations>::Name_Hold);
    const std::vector<VocabularyCatalog::QueryRow> queries = catalog.ListQueries();
    ASSERT_EQ(queries.size(), 33u);
    EXPECT_EQ(queries[5].Name, AuthoredApiDefinition<PhysicsQueryOperations>::Name_Trace);
    EXPECT_EQ(queries[6].Name, AuthoredApiDefinition<PhysicsQueryOperations>::Name_Overlap);
    EXPECT_EQ(queries[8].Name, AuthoredApiDefinition<NavigationOperations>::Name_Phase);
    EXPECT_EQ(queries[9].Name, AuthoredApiDefinition<NavigationOperations>::Name_Failure);
    EXPECT_EQ(queries[10].Name, AuthoredApiDefinition<NavigationOperations>::Name_Remaining);
    EXPECT_EQ(queries[15].Name, AuthoredApiDefinition<CounterOperations>::Name_Value);
    EXPECT_EQ(queries[16].Name, AuthoredApiDefinition<InventoryOperations>::Name_Count);
    EXPECT_EQ(queries[16].ArgumentCount, 4u);
    EXPECT_EQ(queries[17].Name, AuthoredApiDefinition<InventoryOperations>::Name_Entry);
    EXPECT_EQ(queries[18].Name, AuthoredApiDefinition<InventoryOperations>::Name_Property);
    EXPECT_EQ(queries[19].Name, AuthoredApiDefinition<InventoryOperations>::Name_Total);
    EXPECT_EQ(queries[20].Name, AuthoredApiDefinition<EquipmentOperations>::Name_InSlot);
    EXPECT_EQ(queries[21].Name, AuthoredApiDefinition<EquipmentOperations>::Name_Sources);
}

TEST(VocabularyCatalog, TheEnginesPerceptionQueriesAreThereBeforeAnyModuleIs)
{
    VocabularyCatalog catalog;
    EXPECT_TRUE(catalog.Errors().empty());
    const std::vector<VocabularyCatalog::QueryRow> queries = catalog.ListQueries();
    // After the physics, navigation, counter, inventory and equipment queries, which the engine's own scope declares first.
    ASSERT_EQ(queries.size(), 33u);
    const std::size_t sees = static_cast<std::size_t>(std::ranges::find(queries, std::string(AuthoredApiDefinition<PerceptionOperations>::Name_Sees), &VocabularyCatalog::QueryRow::Name) - queries.begin());
    ASSERT_LT(sees, queries.size());
    EXPECT_EQ(queries[sees + 0].Name, AuthoredApiDefinition<PerceptionOperations>::Name_Sees);
    EXPECT_EQ(queries[sees + 0].Provider, "engine");
    EXPECT_EQ(queries[sees + 0].ArgumentCount, 2u);
    EXPECT_EQ(queries[sees + 1].Name, AuthoredApiDefinition<PerceptionOperations>::Name_SeesTagged);
    EXPECT_EQ(queries[sees + 2].Name, AuthoredApiDefinition<PerceptionOperations>::Name_NearestSeenTagged);
    // Then what an observer remembers, which outlasts what it sees.
    EXPECT_EQ(queries[sees + 3].Name, AuthoredApiDefinition<PerceptionOperations>::Name_Remembers);
    EXPECT_EQ(queries[sees + 4].Name, AuthoredApiDefinition<PerceptionOperations>::Name_LastSeenPosition);
    EXPECT_EQ(queries[sees + 5].Name, AuthoredApiDefinition<PerceptionOperations>::Name_LastSeenVelocity);
    EXPECT_EQ(queries[sees + 6].Name, AuthoredApiDefinition<PerceptionOperations>::Name_SecondsSinceSeen);
    EXPECT_EQ(queries[sees + 7].Name, AuthoredApiDefinition<PerceptionOperations>::Name_SecondsTracked);
    EXPECT_EQ(queries[sees + 8].Name, AuthoredApiDefinition<PerceptionOperations>::Name_NearestRememberedTagged);
    // Then claims, which any group's members hold.
    EXPECT_EQ(queries[sees + 9].Name, AuthoredApiDefinition<ClaimOperations>::Name_Count);
    EXPECT_EQ(queries[sees + 10].Name, AuthoredApiDefinition<ClaimOperations>::Name_Held);
}

TEST(VocabularyCatalog, ALoadedModulesNamesAppearBesideTheEnginesWithoutItsRuntime)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    catalog.InstallModuleVocabulary(game);
    EXPECT_EQ(game.Calls, 1);
    EXPECT_TRUE(catalog.Errors().empty());

    const std::vector<VocabularyCatalog::VerbRow> verbs = catalog.ListVerbs();
    ASSERT_EQ(verbs.size(), 58u);
    EXPECT_EQ(verbs[57].Name, kModuleVerb);
    EXPECT_EQ(verbs[57].Provider, "spike");
    EXPECT_EQ(verbs[57].ArgumentCount, 1u);
}

TEST(VocabularyCatalog, ARecordIsReportedResolvedOrNotAndNeverLost)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    catalog.InstallModuleVocabulary(game);

    VerbBindingLibrary library;
    library.Bindings.push_back(Record("menu.resume", std::string(AuthoredApiDefinition<RuntimeResumeOperation>::Name_Resume)));
    VerbBindingDesc fire = Record("hook.fire", kModuleVerb);
    VerbBindingArgument range;
    range.Key = "Range";
    range.Source = VerbArgumentSource::Literal;
    range.Literal = JsonValue(12.0);
    fire.Arguments.push_back(std::move(range));
    library.Bindings.push_back(std::move(fire));
    library.Bindings.push_back(Record("orphan", "never.declared"));

    const std::vector<VocabularyCatalog::BindingRow> rows = catalog.Inspect(library);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].Resolved);
    EXPECT_TRUE(rows[1].Resolved);
    EXPECT_FALSE(rows[2].Resolved);
    EXPECT_NE(rows[2].Error.find("never.declared"), std::string::npos);
    // Inspection reads; the source records are exactly what they were.
    EXPECT_EQ(library.Bindings[2].VerbName, "never.declared");
    EXPECT_EQ(library.Bindings.size(), 3u);
}

TEST(VocabularyCatalog, AModulesRefusedDeclarationIsReportedNotHidden)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    game.DeclareBadly = true;
    catalog.InstallModuleVocabulary(game);
    ASSERT_FALSE(catalog.Errors().empty());
    EXPECT_NE(catalog.Errors().front().find("spike..broken"), std::string::npos);
    // The whole batch was refused: the good name did not land either.
    EXPECT_EQ(catalog.ListVerbs().size(), 57u);
}

TEST(VocabularyCatalog, TwoCatalogsAreTwoWorlds)
{
    VocabularyCatalog first;
    VocabularyCatalog second;
    EXPECT_NE(first.Verbs().Catalog(), second.Verbs().Catalog());
}

namespace
{
// A module whose hook declares a question and an announcement beside a verb,
// through one scope, the way generated companions are declared.
class LampGame final : public Game
{
public:
    void OnRegisterVocabulary(World& world) override
    {
        AuthoredQueryDefinition lit;
        lit.Name = "lamp.lit";
        lit.DisplayName = "Lit";
        DataFieldSchema lamp;
        lamp.Key = "entity";
        lamp.Kind = DataFieldKind::Entity;
        lamp.Reference.ComponentIdentity = "game.lamp";
        lit.Arguments.Children.push_back(std::move(lamp));
        lit.Result.Kind = DataFieldKind::Bool;
        AuthoredQueryRegistrationScope queries(*FindAuthoredQueryRegistry(world), "lamp");
        (void)queries.Declare(std::move(lit));
        (void)queries.Commit();

        AuthoredEventDefinition switched;
        switched.Name = "lamp.switched";
        switched.SourceComponent = "game.lamp";
        AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(world), "lamp");
        (void)events.Declare(std::move(switched));
        (void)events.Commit();
    }
};
}

TEST(VocabularyCatalog, AModulesQueriesAndEventsAreListedBesideItsVerbs)
{
    VocabularyCatalog catalog;
    LampGame game;
    catalog.InstallModuleVocabulary(game);
    EXPECT_TRUE(catalog.Errors().empty());

    const std::vector<VocabularyCatalog::QueryRow> queries = catalog.ListQueries();
    // The engine's own queries come first; the module's follow them.
    ASSERT_FALSE(queries.empty());
    const VocabularyCatalog::QueryRow& lit = queries.back();
    EXPECT_EQ(lit.Name, "lamp.lit");
    EXPECT_EQ(lit.DisplayName, "Lit");
    EXPECT_EQ(lit.Provider, "lamp");
    EXPECT_EQ(lit.ArgumentCount, 1u);

    const std::vector<VocabularyCatalog::EventRow> events = catalog.ListEvents();
    ASSERT_EQ(events.size(), 9u);
    EXPECT_EQ(events[0].Name, kSensorEnterEvent);
    EXPECT_EQ(events[1].Name, kSensorStayEvent);
    EXPECT_EQ(events[2].Name, kSensorExitEvent);
    EXPECT_EQ(events[3].Name, kSensorInteractEvent);
    EXPECT_EQ(events[0].PayloadCount, 1u);
    EXPECT_EQ(events[4].Name, kProjectileHitEvent);
    EXPECT_EQ(events[4].PayloadCount, 6u);
    EXPECT_EQ(events[5].Name, kInteractUseEvent);
    EXPECT_EQ(events[5].PayloadCount, 1u);
    EXPECT_EQ(events[6].Name, kLevelArrivedEvent);
    EXPECT_EQ(events[6].PayloadCount, 1u);
    EXPECT_EQ(events[7].Name, kCounterChangedEvent);
    EXPECT_EQ(events[7].PayloadCount, 3u);
    EXPECT_EQ(events[8].Name, "lamp.switched");
    EXPECT_EQ(events[8].SourceComponent, "game.lamp");
    EXPECT_EQ(events[8].PayloadCount, 0u);
}
