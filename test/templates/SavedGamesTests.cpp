#include "TemplateModuleRun.h"

#include <app/LevelChange.h>
#include <app/VocabularyCommands.h>
#include <core/console/ConsoleService.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/AppliedEffects.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <jobs/AsyncTaskQueue.h>
#include <inventory/InventoryComponents.h>
#include <logic/CounterVocabulary.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/RestoredBodies.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/RigidBody.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/PhysicsWorld.h>
#include <physics/SensorOverlaps.h>
#include <save/SavedGame.h>
#include <save/SavedGames.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/transform/TransformComponents.h>
#include <zone/KeptWithZone.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneStateLists.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(TEST_FPS_MODULE_PATH)
namespace
{
    using FrameScript = std::function<void(Engine&, World&, int frame)>;
    FrameScript Script;

    struct ScriptProbe
    {
        Engine* Host = nullptr;
        int Frames = 0;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (Script)
                Script(*Host, ctx.Entities, ++Frames);
        }
    };

    std::filesystem::path RootFor(std::string_view name)
    {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "sencha_saved_games" / name;
        std::filesystem::remove_all(root);
        return root;
    }

    void RunFps(std::string_view level, int frames, const std::filesystem::path& root, FrameScript script,
                double tickRate = 60.0, std::function<void(EngineConfig&)> adjust = {})
    {
        Script = std::move(script);
        {
            TemplateModuleRun<ScriptProbe> run(TEST_FPS_MODULE_PATH, SENCHA_REPO_ROOT "/templates/fps/assets", level,
                                               frames, [&root, tickRate, &adjust](EngineConfig& config) {
                                                   config.Runtime.SavesRoot = root.string();
                                                   config.Runtime.FixedTickRate = tickRate;
                                                   if (adjust)
                                                       adjust(config);
                                               });
            EXPECT_TRUE(run.Loaded());
            EXPECT_EQ(run.Exit(), 0);
        }
        Script = {};
    }

    std::filesystem::path SlotFile(const std::filesystem::path& root, std::string_view slot)
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             it != std::filesystem::recursive_directory_iterator(); ++it)
            if (it->path().filename() == std::string(slot) + ".ssav")
                return it->path();
        return {};
    }

    std::filesystem::path BackupOf(std::filesystem::path path)
    {
        path += ".bak";
        return path;
    }

    std::optional<SavedGame> ReadSave(const std::filesystem::path& path)
    {
        std::vector<std::byte> bytes;
        if (path.empty() || !ReadFileBytes(path, bytes))
            return std::nullopt;
        return DecodeSavedGame(bytes).Saved;
    }

    bool WriteSave(const std::filesystem::path& path, const SavedGame& saved)
    {
        return WriteFileBytes(path, EncodeSavedGame(saved));
    }

    const SavedGameSection* SectionOf(const SavedGame& saved, std::string_view name)
    {
        for (const SavedGameSection& section : saved.Sections)
            if (section.Name == name)
                return &section;
        return nullptr;
    }

    std::size_t SectionsNamed(const SavedGame& saved, std::string_view name)
    {
        std::size_t count = 0;
        for (const SavedGameSection& section : saved.Sections)
            count += section.Name == name ? 1 : 0;
        return count;
    }

    EntityId AuthoredOtherThan(World& world, EntityId body, PersistentEntityId skip = {})
    {
        EntityId found;
        world.ForEachComponent<PersistentIdComponent>([&](EntityId entity, const PersistentIdComponent& id) {
            if (!found.IsValid() && entity != body && id.Id != skip && IsAuthoredPersistentEntityId(id.Id))
                found = entity;
        });
        return found;
    }

    PersistentEntityId DestroyAuthored(World& world, PersistentEntityId skip = {})
    {
        const EntityId lost = AuthoredOtherThan(world, LocalControlSubjectOf(world), skip);
        const PersistentEntityId id = world.TryGet<PersistentIdComponent>(lost)->Id;
        world.DestroyEntity(lost);
        return id;
    }

    bool HasBody(const World& world) { return LocalControlSubjectOf(world).IsValid(); }

    bool Resolves(const World& world, PersistentEntityId id)
    {
        const PersistentEntityIndex* index = world.TryGetResource<PersistentEntityIndex>();
        return index != nullptr && index->TryResolve(id).IsValid();
    }

    bool RestoreArrived(const Engine& engine)
    {
        return engine.Saves().RestoreStatus() == RestoreProgress::BodiesRestored;
    }

    struct WorldShape
    {
        std::size_t Entities = 0;
        std::map<std::string, std::size_t> Populations;
        std::uint64_t NextId = 0;
    };

    WorldShape ShapeOf(const World& world)
    {
        WorldShape shape{ .Entities = world.EntityCount(), .Populations = {}, .NextId = 1 };
        for (const auto& archetype : world.GetArchetypes())
        {
            std::size_t rows = 0;
            for (const auto& chunk : archetype->Chunks)
                rows += chunk->RowCount;
            if (rows > 0)
                shape.Populations[archetype->Signature.to_string()] += rows;
        }
        if (const PersistentIdMint* mint = world.TryGetResource<PersistentIdMint>())
            shape.NextId = mint->Next;
        return shape;
    }

    bool HasPersistentId(const World& world, EntityId entity)
    {
        return world.IsAlive(entity) && world.TryGet<PersistentIdComponent>(entity) != nullptr;
    }

    const JsonValue* LocalBodyIn(const SavedGame& saved)
    {
        const SavedGameSection* bodies = SectionOf(saved, "sencha.bodies");
        return bodies != nullptr ? bodies->Data.Find("local") : nullptr;
    }

    struct TwoSaves
    {
        PersistentEntityId First;
        PersistentEntityId Second;
    };

    TwoSaves WriteTwoSaves(const std::filesystem::path& root)
    {
        TwoSaves lost;
        int stage = 0;
        RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
            SavedGames& saves = engine.Saves();
            if (stage == 0 && HasBody(world))
            {
                lost.First = DestroyAuthored(world);
                EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
                stage = 1;
            }
            else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
            {
                lost.Second = DestroyAuthored(world, lost.First);
                EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
                stage = 2;
            }
            else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
            {
                stage = 3;
            }
        });
        EXPECT_EQ(stage, 3) << "the two saves were not both written";
        return lost;
    }
}

TEST(SavedGames, ARestoreFromTheBackupKeepsItAsTheBackupThroughTheNextWrite)
{
    const std::filesystem::path root = RootFor("backup_kept");
    const TwoSaves lost = WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::vector<std::byte> bytes;
    ASSERT_TRUE(ReadFileBytes(current, bytes));
    bytes[bytes.size() / 2] ^= std::byte{ 0x40 };
    ASSERT_TRUE(WriteFileBytes(current, bytes));

    bool restored = false;
    bool secondIsBack = false;
    bool firstStaysLost = false;
    SaveStatus written = SaveStatus::Idle;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            restored = saves.RestoredFromBackup();
            secondIsBack = Resolves(world, lost.Second);
            firstStaysLost = !Resolves(world, lost.First);
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2)
        {
            written = saves.WriteStatus();
        }
    });
    ASSERT_TRUE(restored) << "a corrupt file with a good backup did not restore from it";
    EXPECT_TRUE(secondIsBack) << "the restore did not come from the backup";
    EXPECT_TRUE(firstStaysLost);
    EXPECT_EQ(written, SaveStatus::Done);
    EXPECT_TRUE(ReadSave(current).has_value());
    EXPECT_TRUE(ReadSave(BackupOf(current)).has_value()) << "the corrupt file replaced the good backup";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, HoldsAnswersWhatARestoreWouldFind)
{
    const std::filesystem::path root = RootFor("holds");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    ASSERT_TRUE(std::filesystem::remove(current));
    std::filesystem::path broken = current.parent_path() / "broken.ssav";
    ASSERT_TRUE(WriteFileText(broken, "not a save"));

    std::optional<bool> backupOnly;
    std::optional<bool> corruptOnly;
    RunFps("levels/bumper", 60, root, [&](Engine& engine, World&, int frame) {
        if (frame == 30)
        {
            backupOnly = engine.Saves().Holds("default");
            corruptOnly = engine.Saves().Holds("broken");
        }
    });
    ASSERT_TRUE(backupOnly.has_value());
    EXPECT_TRUE(*backupOnly) << "only a backup is left, and a restore would read it";
    EXPECT_FALSE(*corruptOnly) << "a restore cannot read a corrupt file with no backup";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AFileFromAnEarlierRunIsHeldButRememberedOnlyOnceThisSessionWritesOrRestoresIt)
{
    const std::filesystem::path root = RootFor("remembers");
    (void)WriteTwoSaves(root);

    std::optional<bool> heldAtLaunch;
    std::optional<bool> rememberedAtLaunch;
    std::optional<bool> rememberedAfterWrite;
    std::optional<bool> rememberedAfterRestore;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            heldAtLaunch = saves.Holds("default");
            rememberedAtLaunch = saves.Remembers("default");
            EXPECT_FALSE(saves.Remembers("second"));
            EXPECT_EQ(saves.RequestWrite("second"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            rememberedAfterWrite = saves.Remembers("second");
            EXPECT_FALSE(saves.Remembers("default")) << "writing one slot remembered another";
            EXPECT_EQ(saves.RequestRestore("default"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && RestoreArrived(engine))
        {
            rememberedAfterRestore = saves.Remembers("default");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << "the write and the restore did not both finish";
    EXPECT_TRUE(*heldAtLaunch) << "a restore would find the earlier run's file";
    EXPECT_FALSE(*rememberedAtLaunch) << "a file on disk alone is not this session's save";
    EXPECT_TRUE(*rememberedAfterWrite);
    EXPECT_TRUE(*rememberedAfterRestore);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ASaveWithANewerVersionOfARegisteredSectionIsRefused)
{
    const std::filesystem::path root = RootFor("newer_section");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(current);
    ASSERT_TRUE(saved.has_value());
    for (SavedGameSection& section : saved->Sections)
        if (section.Name == "sencha.held")
            section.Version = 99;
    ASSERT_TRUE(WriteSave(current, *saved));

    RestoreProgress status = RestoreProgress::Idle;
    std::string reason;
    RunFps("levels/bumper", 300, root, [&](Engine& engine, World& world, int frame) {
        if (frame == 30)
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
        }
        if (frame > 30)
        {
            status = engine.Saves().RestoreStatus();
            reason = engine.Saves().RestoreFailure();
        }
        (void)world;
    });
    EXPECT_EQ(status, RestoreProgress::Failed) << "a section this build cannot read was dropped without a word";
    EXPECT_NE(reason.find("newer"), std::string::npos) << reason;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AFileFromANewerBuildIsNeitherPassedOverForItsBackupNorLost)
{
    const std::filesystem::path root = RootFor("newer_file");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    HashedChunkFileWriter newer(MakeFourCC('S', 'S', 'A', 'V'), kSavedGameFormatVersion + 1);
    newer.Chunk(MakeFourCC('S', 'U', 'M', 'M'), 1, [&] { return Serialize(newer.Writer(), std::string("{}")); });
    const std::vector<std::byte> newerBytes = newer.Finish(MakeFourCC('H', 'A', 'S', 'H'));
    ASSERT_TRUE(WriteFileBytes(current, newerBytes));

    std::optional<bool> held;
    std::string reason;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            held = saves.Holds("default");
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.RestoreStatus() == RestoreProgress::Failed)
        {
            reason = saves.RestoreFailure();
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << "the restore of a newer build's file did not fail";
    EXPECT_FALSE(*held);
    EXPECT_NE(reason.find("newer"), std::string::npos) << reason;
    std::vector<std::byte> backup;
    ASSERT_TRUE(ReadFileBytes(BackupOf(current), backup));
    EXPECT_EQ(backup, newerBytes) << "the newer build's save was not kept as the backup";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteRequestedDuringALevelChangeKeepsTheBody)
{
    const std::filesystem::path root = RootFor("write_during_change");
    int stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.LevelChanges().Request(
                          LevelChangeRequest::Travel({ .Kind = LevelKind::Scene, .Name = "levels/room_2" }, LevelMemory::Authored,
                                                     LevelChangeBodies::Fresh)),
                      LevelChangeAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1)
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const SavedGameSection* bodies = SectionOf(*saved, "sencha.bodies");
    ASSERT_NE(bodies, nullptr);
    EXPECT_NE(bodies->Data.Find("local"), nullptr) << "the save was written while the player had no body";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteRequestedOnTheLastFrameReachesTheDisk)
{
    const std::filesystem::path root = RootFor("last_frame");
    constexpr int kFrames = 120;
    int requestedOn = 0;
    RunFps("levels/room_2", kFrames, root, [&](Engine& engine, World& world, int frame) {
        if (frame >= kFrames - 1 && HasBody(world) && requestedOn == 0)
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            requestedOn = frame;
        }
    });
    ASSERT_NE(requestedOn, 0);
    EXPECT_TRUE(ReadSave(SlotFile(root, "default")).has_value()) << "the quit dropped the save";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteOnTheAsyncLaneAtQuitStillReachesTheDisk)
{
    const std::filesystem::path root = RootFor("in_flight_at_quit");
    constexpr int kFrames = 120;
    int requestedOn = 0;
    RunFps("levels/room_2", kFrames, root, [&](Engine& engine, World& world, int frame) {
        if (frame >= kFrames - 2 && HasBody(world) && requestedOn == 0)
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            requestedOn = frame;
        }
    });
    ASSERT_NE(requestedOn, 0);
    EXPECT_TRUE(ReadSave(SlotFile(root, "default")).has_value()) << "the quit dropped the save";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ASectionTheSaveCarriedThatRegistersLateIsWrittenOnce)
{
    const std::filesystem::path root = RootFor("unclaimed_once");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(current);
    ASSERT_TRUE(saved.has_value());
    saved->Sections.push_back(SavedGameSection{
        .Name = "future.notes", .Version = 1, .Data = JsonValue(JsonValue::Object{ { "from", JsonValue(std::string("file")) } }) });
    ASSERT_TRUE(WriteSave(current, *saved));

    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            EXPECT_TRUE(saves.RegisterSection(SavedGameSectionDefinition{
                .Name = "future.notes",
                .Capture = [](const World&) { return JsonValue(JsonValue::Object{ { "from", JsonValue(std::string("late")) } }); },
                .Restore = [](World&, std::uint32_t, const JsonValue&) {} }));
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            saves.UnregisterSection("future.notes");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    const std::optional<SavedGame> rewritten = ReadSave(current);
    ASSERT_TRUE(rewritten.has_value());
    EXPECT_EQ(SectionsNamed(*rewritten, "future.notes"), 1u);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, SectionsNothingRegisteredTravelOnlyWithTheGameTheyCameFrom)
{
    const std::filesystem::path root = RootFor("unclaimed_session");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(current);
    ASSERT_TRUE(saved.has_value());
    saved->Sections.push_back(SavedGameSection{ .Name = "future.notes", .Version = 1, .Data = JsonValue(true) });
    ASSERT_TRUE(WriteSave(current, *saved));

    const auto change = [](Engine& engine, LevelChangeKind kind) {
        const LevelReference bumper{ .Kind = LevelKind::Scene, .Name = "levels/bumper" };
        EXPECT_EQ(engine.LevelChanges().Request(
                      kind == LevelChangeKind::NewGame
                          ? LevelChangeRequest::NewGame(bumper)
                          : LevelChangeRequest::Travel(bumper, LevelMemory::Authored, LevelChangeBodies::Fresh)),
                  LevelChangeAdmission::Accepted);
    };
    int stage = 0;
    RunFps("levels/bumper", 900, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        const bool arrived = engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored;
        const bool written = saves.WriteStatus() == SaveStatus::Done;
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            change(engine, LevelChangeKind::Travel);
            stage = 2;
        }
        else if (stage == 2 && arrived)
        {
            EXPECT_EQ(saves.RequestWrite("kept"), SaveAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 3 && written)
        {
            change(engine, LevelChangeKind::NewGame);
            stage = 4;
        }
        else if (stage == 4 && arrived)
        {
            EXPECT_EQ(saves.RequestWrite("fresh"), SaveAdmission::Accepted);
            stage = 5;
        }
        else if (stage == 5 && written)
        {
            stage = 6;
        }
    });
    ASSERT_EQ(stage, 6);
    const std::optional<SavedGame> kept = ReadSave(SlotFile(root, "kept"));
    const std::optional<SavedGame> fresh = ReadSave(SlotFile(root, "fresh"));
    ASSERT_TRUE(kept.has_value() && fresh.has_value());
    EXPECT_EQ(SectionsNamed(*kept, "future.notes"), 1u) << "an ordinary level change is the same game";
    EXPECT_EQ(SectionsNamed(*fresh, "future.notes"), 0u) << "a new game carried the old save's sections";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ALevelChangeThatDiscardsRuntimeStateStartsWithoutTheWorldsCounters)
{
    const std::filesystem::path root = RootFor("discard_counters");
    GameplayTagId counter;
    std::optional<std::int32_t> afterKeep;
    std::optional<std::int32_t> afterDiscard;
    int stage = 0;
    RunFps("levels/room_2", 600, root, [&](Engine& engine, World& world, int) {
        const bool arrived = engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored;
        const auto change = [&](LevelChangeKind kind) {
            const LevelReference room{ .Kind = LevelKind::Scene, .Name = "levels/room_2" };
            EXPECT_EQ(engine.LevelChanges().Request(
                          kind == LevelChangeKind::NewGame
                              ? LevelChangeRequest::NewGame(room)
                              : LevelChangeRequest::Travel(room, LevelMemory::Authored, LevelChangeBodies::Fresh)),
                      LevelChangeAdmission::Accepted);
        };
        if (stage == 0 && HasBody(world))
        {
            counter = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.boss_defeated");
            (void)WriteCounter(world, counter, EntityId{}, 1);
            change(LevelChangeKind::Travel);
            stage = 1;
        }
        else if (stage == 1 && arrived)
        {
            afterKeep = CounterValue(world, counter, EntityId{});
            change(LevelChangeKind::NewGame);
            stage = 2;
        }
        else if (stage == 2 && arrived)
        {
            afterDiscard = CounterValue(world, counter, EntityId{});
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(afterKeep, 1) << "an ordinary level change keeps the game's counters";
    EXPECT_EQ(afterDiscard, 0) << "a new game kept the previous game's counter";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ARestoreIsFollowedToItsBodiesAndCounted)
{
    const std::filesystem::path root = RootFor("restore_progress");
    (void)WriteTwoSaves(root);
    std::vector<RestoreProgress> seen;
    std::uint64_t countBefore = 0;
    std::uint64_t countAfter = 0;
    SaveAdmission repeat = SaveAdmission::Accepted;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            countBefore = saves.Restores();
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            countAfter = saves.Restores();
            stage = 1;
        }
        else if (stage == 1 && saves.RestoreStatus() == RestoreProgress::Loading)
        {
            repeat = saves.RequestRestore();
            stage = 2;
        }
        if (seen.empty() || seen.back() != saves.RestoreStatus())
            seen.push_back(saves.RestoreStatus());
    });
    EXPECT_EQ(countAfter, countBefore + 1);
    EXPECT_EQ(repeat, SaveAdmission::Busy) << "a second restore during the first's level change";
    const std::vector<RestoreProgress> expected{ RestoreProgress::Idle, RestoreProgress::Pending,
                                                 RestoreProgress::Reading, RestoreProgress::Loading,
                                                 RestoreProgress::BodiesRestored };
    EXPECT_EQ(seen, expected);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteThatCannotReachTheDiskFailsAndTheRestoreStillUsesIt)
{
    const std::filesystem::path root = RootFor("unwritable");
    std::filesystem::create_directories(root.parent_path());
    ASSERT_TRUE(WriteFileText(root, "a file where the saves directory would be"));

    SaveAdmission repeat = SaveAdmission::Accepted;
    std::string writeFailure;
    std::string restoreFailure = "unset";
    PersistentEntityId lostBefore;
    PersistentEntityId lostAfter;
    bool lostBeforeStaysLost = false;
    bool lostAfterIsBack = false;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            lostBefore = DestroyAuthored(world);
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            repeat = saves.RequestWrite();
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Failed)
        {
            writeFailure = saves.WriteFailure();
            lostAfter = DestroyAuthored(world, lostBefore);
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && RestoreArrived(engine))
        {
            lostBeforeStaysLost = !Resolves(world, lostBefore);
            lostAfterIsBack = Resolves(world, lostAfter);
            restoreFailure = saves.RestoreFailure();
            stage = 3;
        }
    });
    EXPECT_EQ(repeat, SaveAdmission::Busy);
    ASSERT_EQ(stage, 3) << "the failed write, or the restore after it, did not finish";
    EXPECT_FALSE(writeFailure.empty());
    EXPECT_TRUE(restoreFailure.empty()) << "the restore took the write's failure: " << restoreFailure;
    EXPECT_TRUE(lostBeforeStaysLost) << "the restore went past this session's save";
    EXPECT_TRUE(lostAfterIsBack);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ARestoreOfASaveWithoutHeldRecordsLeavesNoneOfTheOldSessions)
{
    const std::filesystem::path root = RootFor("missing_held");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(current);
    ASSERT_TRUE(saved.has_value());
    std::erase_if(saved->Sections, [](const SavedGameSection& section) { return section.Name == "sencha.held"; });
    ASSERT_TRUE(WriteSave(current, *saved));

    std::optional<std::int32_t> after;
    GameplayTagId counter;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            counter = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.session_counter");
            (void)WriteCounter(world, counter, EntityId{}, 3);
            EXPECT_EQ(CounterValue(world, counter, EntityId{}), 3);
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            after = CounterValue(world, counter, EntityId{});
            stage = 2;
        }
    });
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, 0) << "the previous session's world counter survived a restore";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, TheMapCommandChangesLevelThroughTheLevelChange)
{
    const std::filesystem::path root = RootFor("map_command");
    std::optional<ConsoleStatus> ran;
    PersistentEntityId lost;
    bool lostIsBack = false;
    int stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            lost = DestroyAuthored(world);
            ran = engine.Console().ExecuteLine("map levels/room_2").Status;
            stage = 1;
        }
        else if (stage == 1 && engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored)
        {
            lostIsBack = Resolves(world, lost);
            stage = 2;
        }
    });
    ASSERT_TRUE(ran.has_value());
    EXPECT_EQ(*ran, ConsoleStatus::Ok) << "map with a level loaded did not change level";
    EXPECT_EQ(stage, 2);
    EXPECT_TRUE(lostIsBack) << "the zone state of the level before stayed installed";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ASavedBodyThatNoLongerFitsItsPrefabFailsTheRestore)
{
    const std::filesystem::path root = RootFor("body_misfit");
    (void)WriteTwoSaves(root);
    const std::filesystem::path current = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(current);
    ASSERT_TRUE(saved.has_value());
    bool patched = false;
    for (SavedGameSection& section : saved->Sections)
        if (JsonValue* local = section.Name == "sencha.bodies" ? section.Data.Find("local") : nullptr)
            if (JsonValue* patch = local->Find("patch"); patch != nullptr && patch->IsObject())
            {
                patch->AsObject().emplace_back("no_such_component", JsonValue(JsonValue::Object{}));
                patched = true;
            }
    ASSERT_TRUE(patched);
    ASSERT_TRUE(WriteSave(current, *saved));

    std::string reason;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.RestoreStatus() == RestoreProgress::Failed)
        {
            reason = saves.RestoreFailure();
            stage = 2;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            stage = 3;
        }
    });
    EXPECT_EQ(stage, 2) << "a body the save could not give back was reported as restored";
    EXPECT_NE(reason.find("saved body"), std::string::npos) << reason;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AnUntouchedBodySavesAnEmptyPatch)
{
    const std::filesystem::path root = RootFor("untouched_body");
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const SavedGameSection* bodies = SectionOf(*saved, "sencha.bodies");
    ASSERT_NE(bodies, nullptr);
    const JsonValue* local = bodies->Data.Find("local");
    ASSERT_NE(local, nullptr);
    const JsonValue* patch = local->Find("patch");
    ASSERT_NE(patch, nullptr);
    EXPECT_EQ(JsonStringify(*patch), "{}");
    std::filesystem::remove_all(root);
}

TEST(SavedGames, DescribeSaysWhatASaveHoldsOfTheBody)
{
    const std::filesystem::path root = RootFor("describe_body");
    std::optional<ConsoleResult> described;
    std::optional<ConsoleStatus> refused;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (!described.has_value() && HasBody(world))
        {
            described = engine.Console().ExecuteLine("save.describe me");
            refused = engine.Console().ExecuteLine("save.describe 4000000:7").Status;
        }
    });
    ASSERT_TRUE(described.has_value());
    ASSERT_TRUE(described->Succeeded());
    std::vector<std::string> lines;
    for (const ConsoleOutputEntry& entry : described->Output)
        lines.push_back(entry.Text);
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.front().find("is the local body"), std::string::npos) << lines.front();
    const auto has = [&lines](std::string_view line) { return std::ranges::find(lines, line) != lines.end(); };
    EXPECT_TRUE(has("  CharacterController: authored, follows its prefab"));
    EXPECT_TRUE(has("  CharacterMovement: saved, in the patch where it differs from its prefab"));
    EXPECT_TRUE(has("  LookOrientation: saved, in the patch where it differs from its prefab"));
    EXPECT_TRUE(has("  scene_instance: identity, given by its spawn"));
    EXPECT_EQ(refused, ConsoleStatus::InvalidArguments);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWorldCounterAndABodyCounterComeBackInTheNextRun)
{
    const std::filesystem::path root = RootFor("counters_next_run");
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            const GameplayTagId keys = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.keys");
            (void)WriteCounter(world, keys, EntityId{}, 4);
            (void)WriteCounter(world, keys, LocalControlSubjectOf(world), 2);
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);

    std::optional<std::int32_t> worldKeys;
    std::optional<std::int32_t> bodyKeys;
    GameplayTagId keys;
    stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0)
        {
            keys = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.keys");
            stage = 1;
        }
        else if (stage == 1 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && RestoreArrived(engine))
        {
            worldKeys = CounterValue(world, keys, EntityId{});
            bodyKeys = CounterValue(world, keys, LocalControlSubjectOf(world));
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(worldKeys, 4);
    EXPECT_EQ(bodyKeys, 2) << "the body's counter did not follow the restored body";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ARestoredBodyNobodyTookIsNotGivenToTheNextLevelChange)
{
    const std::filesystem::path root = RootFor("stale_restored_body");
    constexpr Vec3d kStale{ 40.0, 1.0, 40.0 };
    std::optional<Vec3d> at;
    int stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            RestoredBody stale;
            stale.Root.Position = kStale;
            if (!world.HasResource<RestoredBodies>())
                world.AddResource<RestoredBodies>();
            world.GetResource<RestoredBodies>().Local = stale;
            EXPECT_EQ(engine.LevelChanges().Request(
                          LevelChangeRequest::Travel({ .Kind = LevelKind::Scene, .Name = "levels/room_2" }, LevelMemory::Authored,
                                                     LevelChangeBodies::Fresh)),
                      LevelChangeAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored)
        {
            at = world.TryGet<LocalTransform>(LocalControlSubjectOf(world))->Value.Position;
            stage = 2;
        }
    });
    ASSERT_TRUE(at.has_value());
    EXPECT_GT((*at - kStale).Magnitude(), 5.0) << "an ordinary level change spawned the body a restore left behind";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteLeavesTheEntitiesTheirArchetypesAndTheIdMintAsTheyWere)
{
    const std::filesystem::path root = RootFor("write_changes_nothing");
    std::optional<WorldShape> before;
    std::optional<WorldShape> captured;
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            stage = 1;
        }
        else if (stage == 1)
        {
            EXPECT_TRUE(saves.RegisterSection(SavedGameSectionDefinition{
                .Name = "game.shape",
                .Capture = [&captured](const World& seen) {
                    captured = ShapeOf(seen);
                    return JsonValue(JsonValue::Object{});
                },
                .Restore = [](World&, std::uint32_t, const JsonValue&) {} }));
            before = ShapeOf(world);
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            saves.UnregisterSection("game.shape");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    ASSERT_TRUE(before.has_value() && captured.has_value());
    EXPECT_EQ(captured->Entities, before->Entities);
    EXPECT_EQ(captured->Populations, before->Populations) << "capturing moved an entity to another archetype";
    EXPECT_EQ(captured->NextId, before->NextId) << "capturing minted an id";
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const SavedGameSection* ids = SectionOf(*saved, "sencha.persistent_ids");
    ASSERT_NE(ids, nullptr);
    EXPECT_EQ(static_cast<std::uint64_t>(ids->Data.NumberOr("next", 0.0)), before->NextId);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ARecordHeldTheSameTickAsTheWriteIsInTheSave)
{
    const std::filesystem::path root = RootFor("held_same_tick");
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            const EntityId record = world.CreateEntity();
            world.AddComponent<Held>(record, Held{ .Holder = { .Entity = LocalControlSubjectOf(world), .Target = {} } });
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const JsonValue* local = LocalBodyIn(*saved);
    ASSERT_NE(local, nullptr);
    const JsonValue* bodyId = local->Find("id");
    ASSERT_NE(bodyId, nullptr) << "the record names the body, so the body is saved with the id it was named by";
    const SavedGameSection* held = SectionOf(*saved, "sencha.held");
    ASSERT_NE(held, nullptr);
    std::size_t heldByBody = 0;
    for (const JsonValue& record : held->Data.Find("records")->AsArray())
        if (const JsonValue* holder = record.Find("components")->Find("Held")->Find("holder");
            holder != nullptr && holder->AsString() == bodyId->AsString())
            ++heldByBody;
    EXPECT_EQ(heldByBody, 1u);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteWithABodilessLocalParticipantFailsAndTheSlotKeepsItsLastSave)
{
    const std::filesystem::path root = RootFor("bodiless_write");
    PersistentEntityId lostBefore;
    PersistentEntityId lostAfter;
    std::string failure;
    bool lostBeforeStaysLost = false;
    bool lostAfterIsBack = false;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            lostBefore = DestroyAuthored(world);
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            world.DestroyEntity(LocalControlSubjectOf(world));
            lostAfter = DestroyAuthored(world, lostBefore);
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Failed)
        {
            failure = saves.WriteFailure();
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            stage = 5;
        }
        else if (stage == 3 && RestoreArrived(engine))
        {
            lostBeforeStaysLost = !Resolves(world, lostBefore);
            lostAfterIsBack = Resolves(world, lostAfter);
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4) << "the write without a body was not refused, or the restore after it did not arrive";
    EXPECT_EQ(failure, "the local participant has no body");
    EXPECT_TRUE(lostBeforeStaysLost) << "the restore went past the slot's last save";
    EXPECT_TRUE(lostAfterIsBack) << "the refused write replaced the slot's last save";
    const std::optional<SavedGame> onDisk = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(onDisk.has_value());
    EXPECT_NE(LocalBodyIn(*onDisk), nullptr) << "the file on disk lost its body";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWorldWithNoLocalParticipantStillSaves)
{
    const std::filesystem::path root = RootFor("no_local_participant");
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            world.RemoveComponent<LocalParticipant>(LocalParticipantOf(world));
            ASSERT_FALSE(LocalParticipantOf(world).IsValid());
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Failed)
        {
            ADD_FAILURE() << engine.Saves().WriteFailure();
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 2);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(LocalBodyIn(*saved), nullptr);
    std::filesystem::remove_all(root);
}
#endif

#if defined(TEST_FPS_MODULE_PATH)
namespace
{
    const SavedGameSection* HeldSectionOf(const SavedGame& saved) { return SectionOf(saved, "sencha.held"); }

    std::vector<ZoneListEntity> ZoneEntities(const EncodedZoneLists& lists, std::string_view name)
    {
        if (lists == nullptr)
            return {};
        for (const ZoneStateList& list : DecodeZoneLists(*lists).value_or(std::vector<ZoneStateList>{}))
            if (list.Name == name)
                return DecodeEntityList(list.Payload).value_or(std::vector<ZoneListEntity>{});
        return {};
    }

    const DurableLevelState* LevelIn(const SavedGame& saved, const LevelReference& level)
    {
        for (const DurableLevelState& state : saved.Levels)
            if (state.Level == level)
                return &state;
        return nullptr;
    }

    bool HoldsRecord(const World& world, EntityId holder)
    {
        const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
        return index != nullptr && !index->RecordsOf(holder).empty();
    }
}

TEST(SavedGames, ALevelEntitysGrantedTagAndWhatItHoldsComeBackInAFreshRun)
{
    const std::filesystem::path root = RootFor("level_patch_fresh_run");
    const LevelReference room{ .Kind = LevelKind::Scene, .Name = "levels/room_2" };
    PersistentEntityId door;
    PersistentEntityId record;
    int stage = 0;
    RunFps(room.Name, 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            const GameplayTagId unsealed = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.unsealed");
            const EntityId holder = AuthoredOtherThan(world, LocalControlSubjectOf(world));
            door = world.TryGet<PersistentIdComponent>(holder)->Id;
            ASSERT_EQ(GrantBaselineTag(world, holder, unsealed), GameplayTagWrite::Added);
            const EntityId placed = PlaceHeldRecord(world, holder, ArchetypeSignature{});
            ASSERT_TRUE(placed.IsValid());
            record = world.TryGet<PersistentIdComponent>(placed)->Id;
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const DurableLevelState* level = LevelIn(*saved, room);
    ASSERT_NE(level, nullptr) << "the level the player changed is in the file";
    ASSERT_EQ(level->Zones.size(), 1u);
    const std::vector<ZoneListEntity> changed = ZoneEntities(level->Zones[0].Lists, kChangedZoneList);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0].Id, door);
    EXPECT_NE(JsonStringify(changed[0].Value).find("test.unsealed"), std::string::npos);
    const std::vector<ZoneListEntity> held = ZoneEntities(level->Zones[0].Held, kHeldZoneList);
    ASSERT_EQ(held.size(), 1u) << "what a level entity holds is written under its level and zone";
    EXPECT_EQ(held[0].Id, record);
    for (const JsonValue& entry : HeldSectionOf(*saved)->Data.Find("records")->AsArray())
        EXPECT_NE(entry.Find("id")->AsString(), PersistentEntityIdToString(record)) << "and not with the body's";

    bool unsealed = false;
    bool holding = false;
    stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            (void)world.GetResource<GameplayTagRegistry>().RegisterTag("test.unsealed");
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            const PersistentEntityIndex& index = world.GetResource<PersistentEntityIndex>();
            const EntityId holder = index.TryResolve(door);
            const GameplayTagId tag = world.GetResource<GameplayTagRegistry>().FindTag("test.unsealed");
            const GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(holder);
            unsealed = tags != nullptr && tags->HasExact(tag);
            holding = HoldsRecord(world, holder);
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_TRUE(unsealed) << "the granted tag came back with the level";
    EXPECT_TRUE(holding) << "the record waited for its holder and found it";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AfterAWriteOnlyTheSlotsNameIsHeldSoARemovedFileDoesNotRestore)
{
    const std::filesystem::path root = RootFor("only_names");
    bool remembers = false;
    bool holds = true;
    std::string failure;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            const std::filesystem::path file = SlotFile(root, "default");
            std::filesystem::remove(file);
            std::filesystem::remove(BackupOf(file));
            remembers = saves.Remembers("default");
            holds = saves.Holds("default");
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.RestoreStatus() == RestoreProgress::Failed)
        {
            failure = saves.RestoreFailure();
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << "the restore of a removed slot did not fail";
    EXPECT_TRUE(remembers) << "the session still knows it wrote the slot";
    EXPECT_FALSE(holds) << "nothing of the save is held in memory once it is on disk";
    EXPECT_NE(failure.find("default"), std::string::npos) << failure;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AChangedListFromANewerBuildFailsTheRestoreBeforeTheLevelChanges)
{
    const std::filesystem::path root = RootFor("newer_list");
    (void)WriteTwoSaves(root);
    const std::filesystem::path file = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(file);
    ASSERT_TRUE(saved.has_value());
    ASSERT_FALSE(saved->Levels.empty());
    const ZoneStateList newer{ std::string(kChangedZoneList), kZoneListVersion + 1, {} };
    saved->Levels[0].Zones[0].Lists = EncodeZoneLists(std::span(&newer, 1));
    ASSERT_TRUE(WriteSave(file, *saved));

    std::uint64_t changesBefore = 0;
    std::uint64_t changesAfter = 0;
    std::string failure;
    int stage = 0;
    RunFps("levels/bumper", 300, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            changesBefore = engine.LevelChanges().Serial();
            EXPECT_EQ(saves.RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.RestoreStatus() == RestoreProgress::Failed)
        {
            changesAfter = engine.LevelChanges().Serial();
            failure = saves.RestoreFailure();
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_EQ(changesAfter, changesBefore) << "the level changed before the restore was refused";
    EXPECT_NE(failure.find("newer"), std::string::npos) << failure;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AFileWithAThumbnailRestores)
{
    const std::filesystem::path root = RootFor("thumbnail");
    (void)WriteTwoSaves(root);
    const std::filesystem::path file = SlotFile(root, "default");
    std::optional<SavedGame> saved = ReadSave(file);
    ASSERT_TRUE(saved.has_value());
    saved->Thumbnail = SavedGameChunk{ .Id = MakeFourCC('T', 'H', 'M', 'B'), .Version = 1,
                                       .Payload = { std::byte{ 1 }, std::byte{ 2 } }, .InPrefix = true };
    ASSERT_TRUE(WriteSave(file, *saved));

    std::string failure = "unset";
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            failure = engine.Saves().RestoreFailure();
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_TRUE(failure.empty()) << failure;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ASaveNamingAnArrivalHoldsNoTransformAndARestoreStandsTheBodyAtItsLandmark)
{
    const std::filesystem::path root = RootFor("arrival");
    int stage = 0;
    RunFps("levels/bumper", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestWrite("default", "bumper_west"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const JsonValue* local = LocalBodyIn(*saved);
    ASSERT_NE(local, nullptr);
    ASSERT_NE(local->Find("arrival"), nullptr);
    EXPECT_EQ(local->Find("arrival")->AsString(), "bumper_west");
    EXPECT_EQ(local->Find("root"), nullptr) << "a save that names an arrival records no transform for the body";

    Vec3d stood;
    Vec3d moving;
    stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            const EntityId body = LocalControlSubjectOf(world);
            stood = world.TryGet<LocalTransform>(body)->Value.Position;
            moving = world.TryGet<KinematicState>(body)->Velocity;
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_NEAR(stood.X, -6.0f, 0.05f) << "the restored body does not stand at the save's landmark";
    EXPECT_NEAR(stood.Z, 6.0f, 0.05f);
    EXPECT_NEAR(moving.X, 0.0f, 0.01f);
    EXPECT_NEAR(moving.Z, 0.0f, 0.01f);
    std::filesystem::remove_all(root);
}

namespace
{
    constexpr std::string_view kStance = "test.mode.stance";

    LocomotionModeId Stance(World& world) { return world.GetResource<LocomotionModeRegistry>().RegisterFree(std::string(kStance)); }

    const JsonValue* PatchMember(const SavedGame& saved, std::string_view component)
    {
        const JsonValue* local = LocalBodyIn(saved);
        const JsonValue* patch = local != nullptr ? local->Find("patch") : nullptr;
        return patch != nullptr ? patch->Find(component) : nullptr;
    }

    struct RestoredMotion
    {
        Vec3d Velocity;
        LocomotionModeId Mode;
        LocomotionModeId Stance;
        LocomotionModeId Free;
    };

    RestoredMotion RestoreMotion(const std::filesystem::path& root, std::string_view slot, int wait)
    {
        RestoredMotion restored;
        int stage = 0;
        RunFps("levels/bumper", 500, root, [&](Engine& engine, World& world, int frame) {
            if (stage == 0 && HasBody(world) && frame > wait)
            {
                restored.Stance = Stance(world);
                restored.Free = world.GetResource<LocomotionModeRegistry>().FreeMode();
                EXPECT_EQ(engine.Saves().RequestRestore(std::string(slot)), SaveAdmission::Accepted);
                stage = 1;
            }
            else if (stage == 1 && RestoreArrived(engine))
            {
                const EntityId body = LocalControlSubjectOf(world);
                restored.Velocity = world.TryGet<KinematicState>(body)->Velocity;
                restored.Mode = world.TryGet<CharacterMovement>(body)->Mode;
                stage = 2;
            }
        });
        EXPECT_EQ(stage, 2);
        return restored;
    }
}

TEST(SavedGames, ASaveNamingAnArrivalKeepsNoMotionAndAQuicksaveKeepsBoth)
{
    const std::filesystem::path root = RootFor("arrival_motion");
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 || !HasBody(world))
            stage = HasBody(world) ? std::max(stage, 1) : stage;
        if (stage >= 1 && stage <= 4)
        {
            const EntityId body = LocalControlSubjectOf(world);
            world.TryGet<CharacterMovement>(body)->Mode = Stance(world);
            world.TryGet<KinematicState>(body)->Velocity = Vec3d(3.0f, 0.0f, 0.0f);
        }
        if (stage == 1)
        {
            EXPECT_EQ(engine.Saves().RequestWrite("arrived", "bumper_west"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(engine.Saves().RequestWrite("quick"), SaveAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 3 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 5;
        }
    });
    ASSERT_EQ(stage, 5);

    const std::optional<SavedGame> arrived = ReadSave(SlotFile(root, "arrived"));
    const std::optional<SavedGame> quick = ReadSave(SlotFile(root, "quick"));
    ASSERT_TRUE(arrived.has_value() && quick.has_value());
    EXPECT_EQ(PatchMember(*arrived, "KinematicState"), nullptr) << "a save naming an arrival kept the body's velocity";
    EXPECT_EQ(PatchMember(*arrived, "CharacterMovement"), nullptr) << "a save naming an arrival kept the body's mode";
    const JsonValue* velocity = PatchMember(*quick, "KinematicState");
    const JsonValue* mode = PatchMember(*quick, "CharacterMovement");
    ASSERT_NE(velocity, nullptr) << "a quicksave lost the body's velocity";
    ASSERT_NE(mode, nullptr) << "a quicksave lost the body's mode";
    EXPECT_EQ(mode->Find("mode")->AsString(), kStance);
    const double savedX = velocity->Find("velocity")->AsArray()[0].AsNumber();
    EXPECT_GT(savedX, 0.5);

    const RestoredMotion atRest = RestoreMotion(root, "arrived", 30);
    EXPECT_EQ(atRest.Mode, atRest.Free) << "the body arrived in the mode it was saved in";
    EXPECT_NEAR(atRest.Velocity.X, 0.0f, 0.01f) << "the body arrived moving";

    const RestoredMotion moving = RestoreMotion(root, "quick", 45);
    EXPECT_EQ(moving.Mode, moving.Stance) << "a quicksave did not restore the mode the body was simply in";
    EXPECT_NEAR(moving.Velocity.X, savedX, 0.05) << "a quicksave did not restore the body's velocity";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AQuicksaveMidJumpContinuesTheArcInAFreshRunWhoseTickDiffers)
{
    const std::filesystem::path root = RootFor("mid_jump");
    int stage = 0;
    Vec3d savedAt;
    Vec3d savedVelocity;
    std::uint64_t savedTick = 0;
    RunFps("levels/room_2", 1500, root, [&](Engine& engine, World& world, int frame) {
        if (stage == 0 && HasBody(world) && frame > 20)
        {
            world.TryGet<MotionImpulse>(LocalControlSubjectOf(world))->DeltaVelocity = Vec3d(4.0f, 6.0f, 0.0f);
            stage = 1;
        }
        else if (stage == 1)
        {
            const EntityId body = LocalControlSubjectOf(world);
            const Vec3d velocity = world.TryGet<KinematicState>(body)->Velocity;
            if (velocity.Y > 0.0f && velocity.Y < 1.5f)
            {
                savedAt = world.TryGet<LocalTransform>(body)->Value.Position;
                savedVelocity = velocity;
                savedTick = engine.Runtime().GetSimulationClock().GetTickIndex();
                EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
                stage = 2;
            }
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    ASSERT_GT(savedVelocity.X, 1.0f) << "the jump was not under way when it was saved";

    stage = 0;
    Vec3d first;
    Vec3d landed;
    std::uint64_t restoredTick = 0;
    RunFps("levels/room_2", 3000, root, [&](Engine& engine, World& world, int frame) {
        const std::uint64_t tick = engine.Runtime().GetSimulationClock().GetTickIndex();
        if (stage == 0 && HasBody(world) && frame > 600)
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            const EntityId body = LocalControlSubjectOf(world);
            first = world.TryGet<KinematicState>(body)->Velocity;
            restoredTick = tick;
            stage = 2;
        }
        else if (stage == 2 && tick >= restoredTick + 60)
        {
            landed = world.TryGet<LocalTransform>(LocalControlSubjectOf(world))->Value.Position;
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_NE(restoredTick, savedTick) << "the restore ran on the tick the save was written on";
    EXPECT_NEAR(first.X, savedVelocity.X, 0.05f) << "the restored body did not take its saved velocity";
    EXPECT_NEAR(first.Y, savedVelocity.Y, 0.05f);
    EXPECT_GT(landed.X - savedAt.X, 1.0f) << "the restored body dropped straight down instead of finishing its arc";
    std::filesystem::remove_all(root);
}
#endif

#if defined(TEST_FPS_MODULE_PATH)
static_assert(!std::is_aggregate_v<LevelChangeRequest>, "a request is built only through its named constructors");
static_assert(!std::is_default_constructible_v<LevelChangeRequest>, "a request states what it keeps");

namespace
{
    const LevelReference kFirst{ .Kind = LevelKind::Scene, .Name = "levels/bumper" };
    const LevelReference kSecond{ .Kind = LevelKind::Scene, .Name = "levels/room_2" };

    bool Arrived(const Engine& engine) { return engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored; }

    void Travel(Engine& engine, const LevelReference& level, LevelMemory memory)
    {
        EXPECT_EQ(engine.LevelChanges().Request(LevelChangeRequest::Travel(level, memory, LevelChangeBodies::Fresh)),
                  LevelChangeAdmission::Accepted);
    }

    std::vector<PersistentEntityId> AuthoredIds(const World& world)
    {
        std::vector<PersistentEntityId> ids;
        const EntityId body = LocalControlSubjectOf(world);
        world.ForEachComponent<PersistentIdComponent>([&](EntityId entity, const PersistentIdComponent& id) {
            if (entity != body && IsAuthoredPersistentEntityId(id.Id) && !world.HasComponent<Held>(entity))
                ids.push_back(id.Id);
        });
        std::ranges::sort(ids, {}, &PersistentEntityId::Value);
        return ids;
    }

    EntityId Live(const World& world, PersistentEntityId id)
    {
        return world.TryGetResource<PersistentEntityIndex>()->TryResolve(id);
    }

    struct Changes
    {
        PersistentEntityId Door;
        PersistentEntityId Chest;
        PersistentEntityId Pickup;
        PersistentEntityId Record;
        EntityId RecordEntity;

        void Make(World& world)
        {
            const std::vector<PersistentEntityId> ids = AuthoredIds(world);
            ASSERT_GE(ids.size(), 2u) << "the level authors too few entities for the test";
            Door = ids[0];
            Chest = ids[0];
            Pickup = ids[1];
            const GameplayTagId unsealed = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.unsealed");
            ASSERT_EQ(GrantBaselineTag(world, Live(world, Door), unsealed), GameplayTagWrite::Added);
            RecordEntity = PlaceHeldRecord(world, Live(world, Chest), ArchetypeSignature{});
            ASSERT_TRUE(RecordEntity.IsValid());
            Record = world.TryGet<PersistentIdComponent>(RecordEntity)->Id;
            world.DestroyEntity(Live(world, Pickup));
        }

        [[nodiscard]] bool DoorUnsealed(World& world) const
        {
            const GameplayTagId unsealed = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.unsealed");
            const GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(Live(world, Door));
            return tags != nullptr && tags->HasExact(unsealed);
        }

        [[nodiscard]] bool ChestHoldsTheRecord(const World& world) const
        {
            const HeldRecordIndex& index = *world.TryGetResource<HeldRecordIndex>();
            const EntityId chest = Live(world, Chest);
            return chest.IsValid() && std::ranges::any_of(index.RecordsOf(chest), [&](EntityId record) {
                return world.TryGet<PersistentIdComponent>(record)->Id == Record;
            });
        }

        [[nodiscard]] bool PickupGone(const World& world) const { return !Live(world, Pickup).IsValid(); }
    };
}

TEST(SavedGames, AJourneyThereAndBackKeepsWhatTheGameChangedInTheFirstLevel)
{
    const std::filesystem::path root = RootFor("there_and_back");
    Changes changes;
    bool unsealed = false;
    bool holding = false;
    bool gone = false;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            changes.Make(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            unsealed = changes.DoorUnsealed(world);
            holding = changes.ChestHoldsTheRecord(world);
            gone = changes.PickupGone(world);
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_TRUE(unsealed) << "the opened door shut while the player was away";
    EXPECT_TRUE(holding) << "the chest's contents were lost while the player was away";
    EXPECT_TRUE(gone) << "the taken pickup came back";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ASaveWrittenInTheSecondLevelHoldsTheFirstAndAFreshRunGetsItBack)
{
    const std::filesystem::path root = RootFor("save_holds_both");
    Changes changes;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            changes.Make(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->Summary.Level, kSecond);
    const DurableLevelState* first = LevelIn(*saved, kFirst);
    ASSERT_NE(first, nullptr) << "the level left behind is in the save";
    const std::vector<ZoneListEntity> held = ZoneEntities(first->Zones[0].Held, kHeldZoneList);
    ASSERT_EQ(held.size(), 1u) << "the chest's record is written under the chest's level";
    EXPECT_EQ(held[0].Id, changes.Record);

    bool unsealed = false;
    bool holding = false;
    bool gone = false;
    stage = 0;
    RunFps(kSecond.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            (void)world.GetResource<GameplayTagRegistry>().RegisterTag("test.unsealed");
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            unsealed = changes.DoorUnsealed(world);
            holding = changes.ChestHoldsTheRecord(world);
            gone = changes.PickupGone(world);
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_TRUE(unsealed);
    EXPECT_TRUE(holding);
    EXPECT_TRUE(gone);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, EnteringALevelAsAuthoredResetsItAloneAndDestroysWhatWaitedInIt)
{
    const std::filesystem::path root = RootFor("authored_entry");
    Changes changes;
    PersistentEntityId elsewhere;
    bool unsealed = true;
    bool holding = true;
    bool recordAlive = true;
    bool pickupBack = false;
    bool elsewhereKept = false;
    int stage = 0;
    RunFps(kFirst.Name, 1200, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            changes.Make(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            elsewhere = DestroyAuthored(world);
            Travel(engine, kFirst, LevelMemory::Authored);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            unsealed = changes.DoorUnsealed(world);
            holding = changes.ChestHoldsTheRecord(world);
            recordAlive = world.IsAlive(changes.RecordEntity);
            pickupBack = !changes.PickupGone(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine))
        {
            elsewhereKept = !Resolves(world, elsewhere);
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4);
    EXPECT_FALSE(unsealed);
    EXPECT_FALSE(holding);
    EXPECT_FALSE(recordAlive) << "the record waiting in the level entered as authored is destroyed";
    EXPECT_TRUE(pickupBack);
    EXPECT_TRUE(elsewhereKept) << "only the level entered as authored was reset";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ANewGameResetsEveryLevel)
{
    const std::filesystem::path root = RootFor("new_game_levels");
    Changes changes;
    PersistentEntityId elsewhere;
    bool unsealed = true;
    bool elsewhereBack = false;
    int stage = 0;
    RunFps(kFirst.Name, 1200, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            changes.Make(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            elsewhere = DestroyAuthored(world);
            EXPECT_EQ(engine.LevelChanges().Request(LevelChangeRequest::NewGame(kFirst)), LevelChangeAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            unsealed = changes.DoorUnsealed(world);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine))
        {
            elsewhereBack = Resolves(world, elsewhere);
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4);
    EXPECT_FALSE(unsealed);
    EXPECT_TRUE(elsewhereBack);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AChangeCarriesItsArrivalName)
{
    const std::filesystem::path root = RootFor("arrival_name");
    std::string arrival;
    int stage = 0;
    RunFps(kFirst.Name, 600, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.LevelChanges().Request(
                          LevelChangeRequest::Travel(kSecond, LevelMemory::Remembered, LevelChangeBodies::Fresh)
                              .At("west_door")),
                      LevelChangeAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            arrival = world.GetResource<LevelArrival>().Name;
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_EQ(arrival, "west_door");
    std::filesystem::remove_all(root);
}

namespace
{
    const LevelReference kWorld{ .Kind = LevelKind::World, .Name = "traversal3" };

    EntityId WorldSceneEntity(const World& world)
    {
        EntityId found;
        world.ForEachComponent<PersistentIdComponent>([&](EntityId entity, const PersistentIdComponent& id) {
            if (!found.IsValid() && IsAuthoredPersistentEntityId(id.Id)
                && world.GetEntityPartition(entity) == PersistentStoragePartition && !world.HasComponent<Held>(entity))
                found = entity;
        });
        return found;
    }
}

TEST(SavedGames, AWorldScenesChestKeepsWhatItHoldsAcrossAJourneyThereAndBack)
{
    const std::filesystem::path root = RootFor("world_scene_chest");
    PersistentEntityId chest;
    PersistentEntityId record;
    bool holding = false;
    int stage = 0;
    RunFps(kFirst.Name, 1500, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            Travel(engine, kWorld, LevelMemory::Authored);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            const EntityId holder = WorldSceneEntity(world);
            ASSERT_TRUE(holder.IsValid()) << "the world scene authors no entity to hold a record";
            chest = world.TryGet<PersistentIdComponent>(holder)->Id;
            const EntityId placed = PlaceHeldRecord(world, holder, ArchetypeSignature{});
            ASSERT_TRUE(placed.IsValid());
            record = world.TryGet<PersistentIdComponent>(placed)->Id;
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            Travel(engine, kWorld, LevelMemory::Remembered);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine))
        {
            const HeldRecordIndex& index = *world.TryGetResource<HeldRecordIndex>();
            const EntityId holder = Live(world, chest);
            holding = holder.IsValid() && std::ranges::any_of(index.RecordsOf(holder), [&](EntityId held) {
                return world.TryGet<PersistentIdComponent>(held)->Id == record;
            });
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4);
    EXPECT_TRUE(holding) << "what the world scene's entity held was lost while the player was away";
    std::filesystem::remove_all(root);
}

namespace
{
    struct KeptBody
    {
        EntityId Body;
        EntityId Effect;
        EntityId Record;
        GameplayTagId Counter;
        std::uint32_t Remaining = 0;
        Vec3d Position;

        [[nodiscard]] bool Make(Engine& engine, World& world)
        {
            Body = LocalControlSubjectOf(world);
            if (!engine.Schedule().Get<PhysicsStepSystem>()->GetCharacterMovers().SetPosition(
                    world, Body, Vec3d{ 3.0f, 1.0f, 3.0f }))
                return false;
            EffectDefinition timed;
            timed.Duration = EffectDuration::Duration;
            timed.DurationSeconds = 60.0f;
            const EffectId id = world.GetResource<EffectRegistry>().Register("test.timed", timed);
            Effect = ApplyEffect(world, Body, id, engine.Runtime().GetSimulationClock().TimeOfTick(0));
            Counter = *world.GetResource<GameplayTagRegistry>().RegisterTag("test.kept_counter");
            (void)WriteCounter(world, Counter, Body, 3);
            Record = PlaceHeldRecord(world, Body, ArchetypeSignature{});
            EXPECT_TRUE(Effect.IsValid());
            EXPECT_TRUE(Record.IsValid());
            Remaining = Effect.IsValid() ? world.TryGet<ActiveEffect>(Effect)->RemainingTicks : 0;
            Position = world.TryGet<LocalTransform>(Body)->Value.Position;
            return true;
        }

        [[nodiscard]] bool StoodStill(const World& world) const
        {
            const ActiveEffect* effect = world.IsAlive(Effect) ? world.TryGet<ActiveEffect>(Effect) : nullptr;
            const LocalTransform* placed = world.TryGet<LocalTransform>(Body);
            return effect != nullptr && effect->RemainingTicks == Remaining && placed != nullptr
                && placed->Value.Position.X == Position.X && placed->Value.Position.Y == Position.Y
                && placed->Value.Position.Z == Position.Z;
        }
    };

    bool Changing(const Engine& engine)
    {
        const LevelChangeStatus status = engine.LevelChanges().Status();
        return status == LevelChangeStatus::Pending || status == LevelChangeStatus::Loading
            || status == LevelChangeStatus::Resident;
    }
}

TEST(SavedGames, ABodyKeptAcrossATravelIsTheSameBodyHeldStillThroughTheGapAndPlacedOnArrival)
{
    const std::filesystem::path root = RootFor("kept_body");
    KeptBody kept;
    int gapFrames = 0;
    std::uint64_t departedAt = 0;
    std::uint64_t gapTicks = 0;
    bool stoodStill = true;
    bool sameBody = false;
    std::uint32_t remaining = 0;
    std::int32_t counter = 0;
    bool holding = false;
    Vec3d arrived;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        const std::uint64_t tick = engine.Runtime().GetSimulationClock().GetTickIndex();
        if (stage == 0 && HasBody(world) && kept.Make(engine, world))
        {
            EXPECT_EQ(engine.LevelChanges().Request(
                          LevelChangeRequest::Travel(kSecond, LevelMemory::Remembered, LevelChangeBodies::Kept)),
                      LevelChangeAdmission::Accepted);
            departedAt = tick;
            stage = 1;
        }
        else if (stage == 1 && Changing(engine))
        {
            ++gapFrames;
            gapTicks = tick - departedAt;
            stoodStill = stoodStill && kept.StoodStill(world);
        }
        else if (stage == 1 && Arrived(engine))
        {
            sameBody = LocalControlSubjectOf(world) == kept.Body;
            if (const ActiveEffect* effect = world.IsAlive(kept.Effect) ? world.TryGet<ActiveEffect>(kept.Effect) : nullptr)
                remaining = effect->RemainingTicks;
            counter = CounterValue(world, kept.Counter, kept.Body);
            const HeldRecordIndex& index = *world.TryGetResource<HeldRecordIndex>();
            holding = std::ranges::find(index.RecordsOf(kept.Body), kept.Record) != index.RecordsOf(kept.Body).end();
            arrived = world.TryGet<LocalTransform>(kept.Body)->Value.Position;
            stage = 2;
        }
    }, 2000.0);
    ASSERT_EQ(stage, 2);
    EXPECT_TRUE(sameBody) << "the body that crossed is not the one that left";
    EXPECT_GT(gapFrames, 0);
    EXPECT_GT(gapTicks, 0u) << "no tick ran while no level stood, so standing still was not tested";
    EXPECT_TRUE(stoodStill) << "the kept body moved or aged while no level stood";
    EXPECT_LE(kept.Remaining - remaining, 8u) << "the timed effect lost " << kept.Remaining - remaining << " ticks";
    EXPECT_GT(remaining, 0u);
    EXPECT_EQ(counter, 3);
    EXPECT_TRUE(holding) << "what the body held did not cross with it";
    EXPECT_NEAR(arrived.X, 0.0f, 0.05f) << "the kept body was not placed where the game spawns";
    EXPECT_NEAR(arrived.Z, 0.0f, 0.05f);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AFreshTravelGivesThePrefabBodyAndDoesNotCarryTheOldOne)
{
    const std::filesystem::path root = RootFor("fresh_body");
    KeptBody old;
    bool newBody = false;
    bool oldGone = false;
    bool effectGone = false;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world) && old.Make(engine, world))
        {
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            const EntityId body = LocalControlSubjectOf(world);
            newBody = body.IsValid() && body != old.Body;
            oldGone = !world.IsAlive(old.Body);
            effectGone = !world.IsAlive(old.Effect) || world.TryGet<ActiveEffect>(old.Effect)->Target != body;
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_TRUE(newBody);
    EXPECT_TRUE(oldGone) << "a fresh travel carried the old body";
    EXPECT_TRUE(effectGone) << "the new body carries the old body's timed effect";
    std::filesystem::remove_all(root);
}

namespace
{
    Vec3d BodyPosition(const World& world)
    {
        return world.TryGet<LocalTransform>(LocalControlSubjectOf(world))->Value.Position;
    }

    void TravelTo(Engine& engine, const LevelReference& level, LevelChangeBodies bodies, std::string arrival)
    {
        EXPECT_EQ(engine.LevelChanges().Request(
                      LevelChangeRequest::Travel(level, LevelMemory::Remembered, bodies).At(std::move(arrival))),
                  LevelChangeAdmission::Accepted);
    }

    Vec3d ArrivalOf(const LevelReference& level, LevelChangeBodies bodies, std::string arrival, std::string_view name)
    {
        const std::filesystem::path root = RootFor(name);
        Vec3d arrived{ 100.0f, 100.0f, 100.0f };
        int stage = 0;
        RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                TravelTo(engine, level, bodies, arrival);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine))
            {
                arrived = BodyPosition(world);
                stage = 2;
            }
        });
        EXPECT_EQ(stage, 2);
        std::filesystem::remove_all(root);
        return arrived;
    }
}

TEST(SavedGames, ALevelChangeNamingAnArrivalStandsTheNewBodyAtItsLandmark)
{
    const Vec3d arrived = ArrivalOf(kSecond, LevelChangeBodies::Fresh, "room_2_east", "arrival_fresh");
    EXPECT_NEAR(arrived.X, 16.0f, 0.05f);
    EXPECT_NEAR(arrived.Z, 3.0f, 0.05f);
}

TEST(SavedGames, AKeptBodyArrivesAtTheLandmarkItsChangeNames)
{
    const Vec3d arrived = ArrivalOf(kSecond, LevelChangeBodies::Kept, "room_2_east", "arrival_kept");
    EXPECT_NEAR(arrived.X, 16.0f, 0.05f);
    EXPECT_NEAR(arrived.Z, 3.0f, 0.05f);
}

TEST(SavedGames, AnArrivalNoLandmarkAnswersFallsBackToTheGamesSpawn)
{
    const Vec3d fresh = ArrivalOf(kSecond, LevelChangeBodies::Fresh, "nowhere", "arrival_unknown_fresh");
    EXPECT_NEAR(fresh.X, 0.0f, 0.05f);
    EXPECT_NEAR(fresh.Z, 0.0f, 0.05f);
    const Vec3d kept = ArrivalOf(kSecond, LevelChangeBodies::Kept, "nowhere", "arrival_unknown_kept");
    EXPECT_NEAR(kept.X, 0.0f, 0.05f);
    EXPECT_NEAR(kept.Z, 0.0f, 0.05f);
}

TEST(SavedGames, ARestoreInAWorldBringsInTheZoneOfItsArrivalAndStandsTheBodyThere)
{
    const std::filesystem::path root = RootFor("arrival_in_world");
    const ZoneId start = *ZoneIdFromString("9c76e74ceb9abd59");
    const ZoneId east = *ZoneIdFromString("429b0e6664e66213");
    ZoneId focusAtWrite;
    int stage = 0;
    RunFps(kFirst.Name, 1500, root, [&](Engine& engine, World& world, int) {
        const RuntimeZoneRecord* neighbour = engine.World().FindZone(east);
        if (stage == 0 && HasBody(world))
        {
            Travel(engine, kWorld, LevelMemory::Authored);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine) && neighbour != nullptr
                 && neighbour->State == RuntimeZoneLoadState::Resident)
        {
            focusAtWrite = engine.Level().PlayZone();
            EXPECT_EQ(engine.Saves().RequestWrite("default", "room_2_east"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(focusAtWrite, start) << "the landmark's zone must not be the focus when the save is written";
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    ASSERT_TRUE(saved->Summary.Focus.has_value());
    EXPECT_EQ(*saved->Summary.Focus, east) << "the save focuses the zone its arrival stands in";

    ZoneId focusAtRestore;
    Vec3d stood;
    stage = 0;
    RunFps(kFirst.Name, 1500, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            focusAtRestore = engine.Level().PlayZone();
            stood = BodyPosition(world);
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_EQ(focusAtRestore, east);
    EXPECT_NEAR(stood.X, 16.0f, 0.05f) << "the restored body does not stand at the landmark in the streamed zone";
    EXPECT_NEAR(stood.Z, 3.0f, 0.05f);
    std::filesystem::remove_all(root);
}

namespace
{
    ConsoleResult RunVerb(Engine& engine, std::vector<std::string> line)
    {
        return RunVerbCommand(VocabularyConsoleTarget{ .Entities = engine.World().Entities(),
                                                       .Verbs = engine.TryVerbs(),
                                                       .Queries = engine.TryAuthoredQueries() },
                              line);
    }

    std::string TextOf(const ConsoleResult& result)
    {
        std::string text;
        for (const ConsoleOutputEntry& line : result.Output)
            text += line.Text + "\n";
        return text;
    }
}

TEST(SavedGames, TheLevelChangeVerbTravelsKeepingTheBodyToItsArrivalAndNamesWhyItRefuses)
{
    const std::filesystem::path root = RootFor("level_change_verb");
    std::string refused;
    std::string accepted;
    EntityId before;
    bool sameBody = false;
    Vec3d arrived;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            refused = TextOf(RunVerb(engine, { "level.change", "level=levels/nowhere", "kind=scene",
                                               "memory=authored", "bodies=fresh" }));
            before = LocalControlSubjectOf(world);
            accepted = TextOf(RunVerb(engine, { "level.change", "level=levels/room_2", "kind=scene",
                                                "memory=remembered", "bodies=kept", "arrival=room_2_east" }));
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            sameBody = LocalControlSubjectOf(world) == before;
            arrived = BodyPosition(world);
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2) << accepted;
    EXPECT_NE(refused.find("not_cooked"), std::string::npos) << refused;
    EXPECT_NE(accepted.find("accepted"), std::string::npos) << accepted;
    EXPECT_TRUE(sameBody);
    EXPECT_NEAR(arrived.X, 16.0f, 0.05f);
    EXPECT_NEAR(arrived.Z, 3.0f, 0.05f);
    std::filesystem::remove_all(root);
}
namespace
{
    constexpr std::string_view kArrows = "asset://items/arrow.sdata";

    ItemId DefineArrows(World& world)
    {
        return world.GetResource<ItemRegistry>().Define(std::string(kArrows),
                                                        ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
    }

    struct DroppedItem
    {
        PersistentEntityId Pickup;
        PersistentEntityId Entry;
        Vec3d At;

        void Drop(Engine& engine, World& world, Vec3d at, std::uint32_t count)
        {
            const EntityId body = LocalControlSubjectOf(world);
            if (!world.HasComponent<ItemContainer>(body))
                world.AddComponent<ItemContainer>(body, ItemContainer{});
            const EntityId entry = Inventory(world, FixedSimTime{}).Give(body, DefineArrows(world), count).Entry;
            ASSERT_TRUE(entry.IsValid());
            Entry = world.TryGet<PersistentIdComponent>(entry)->Id;
            At = at;
            const std::string answer =
                TextOf(RunVerb(engine, { "inventory.drop", "entry=" + PersistentEntityIdToString(Entry),
                                         std::format("at={},{},{}", at.X, at.Y, at.Z) }));
            const EntityId pickup = ItemHolder(world, entry);
            ASSERT_TRUE(world.IsAlive(pickup) && world.HasComponent<ItemPickup>(pickup)) << answer;
            Pickup = world.TryGet<PersistentIdComponent>(pickup)->Id;
        }

        [[nodiscard]] bool LiesWhereItFell(const World& world, std::uint32_t count) const
        {
            const EntityId pickup = Live(world, Pickup);
            if (!pickup.IsValid())
                return false;
            const LocalTransform* placed = world.TryGet<LocalTransform>(pickup);
            const std::vector<EntityId> held = ItemsIn(world, pickup);
            return placed != nullptr && std::abs(placed->Value.Position.X - At.X) < 1e-4f
                && std::abs(placed->Value.Position.Y - At.Y) < 1e-4f
                && std::abs(placed->Value.Position.Z - At.Z) < 1e-4f && held.size() == 1
                && world.TryGet<PersistentIdComponent>(held[0])->Id == Entry
                && world.TryGet<ItemEntry>(held[0])->Count == count && world.HasComponent<KeptWithZone>(pickup);
        }
    };

    std::size_t PickupsIn(const World& world)
    {
        std::size_t count = 0;
        world.ForEachComponent<ItemPickup>([&](EntityId, const ItemPickup&) { ++count; });
        return count;
    }

    std::vector<GainedZoneMember> GainedIn(const DurableZoneState& zone)
    {
        for (const ZoneStateList& list : DecodeZoneLists(*zone.Lists).value_or(std::vector<ZoneStateList>{}))
            if (list.Name == kGainedZoneList)
                return DecodeGainedList(list.Payload).value_or(std::vector<GainedZoneMember>{});
        return {};
    }
}

TEST(SavedGames, AnItemTheBodyDropsLiesInTheLevelsZoneAndIsThereOnReturnButNotInTheNextLevel)
{
    const std::filesystem::path root = RootFor("drop_there_and_back");
    DroppedItem dropped;
    bool inZone = false;
    bool kept = false;
    std::size_t pickupsElsewhere = 1;
    bool back = false;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            ASSERT_EQ(world.GetEntityPartition(LocalControlSubjectOf(world)), PersistentStoragePartition);
            dropped.Drop(engine, world, Vec3d{ 1.5f, 0.25f, -2.0f }, 7);
            const EntityId pickup = Live(world, dropped.Pickup);
            inZone = world.GetEntityPartition(pickup) == engine.World().FindZone(kSceneLevelZone)->Partition;
            kept = world.HasComponent<KeptWithZone>(pickup);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            pickupsElsewhere = PickupsIn(world);
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            back = dropped.LiesWhereItFell(world, 7);
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_TRUE(inZone) << "a drop by a body in the persistent partition lies in the level's zone";
    EXPECT_TRUE(kept);
    EXPECT_EQ(pickupsElsewhere, 0u) << "the dropped item followed the player into the next level";
    EXPECT_TRUE(back) << "the dropped item was not there on return";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AnItemDroppedInTheFirstLevelIsInASaveWrittenInTheSecondAndAFreshRunGetsItBack)
{
    const std::filesystem::path root = RootFor("drop_save_fresh_run");
    DroppedItem dropped;
    int stage = 0;
    RunFps(kFirst.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            dropped.Drop(engine, world, Vec3d{ 1.5f, 0.25f, -2.0f }, 7);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const DurableLevelState* first = LevelIn(*saved, kFirst);
    ASSERT_NE(first, nullptr);
    const std::vector<GainedZoneMember> gained = GainedIn(first->Zones[0]);
    ASSERT_EQ(gained.size(), 1u) << "the first level's zone holds what it gained";
    EXPECT_EQ(gained[0].Form, kWholeGainedForm);
    EXPECT_EQ(WholeGainedMemberId(gained[0].Payload), dropped.Pickup);
    const std::vector<ZoneListEntity> held = ZoneEntities(first->Zones[0].Held, kHeldZoneList);
    ASSERT_EQ(held.size(), 1u) << "and the dropped entry beside it";
    EXPECT_EQ(held[0].Id, dropped.Entry);

    bool back = false;
    stage = 0;
    RunFps(kSecond.Name, 900, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            (void)DefineArrows(world);
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            EXPECT_FALSE(Resolves(world, dropped.Pickup));
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            back = dropped.LiesWhereItFell(world, 7);
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_TRUE(back) << "the item dropped in the first level did not come back in a fresh run";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ADroppedItemIsGoneAfterItsLevelIsEnteredAsAuthoredAndAfterANewGame)
{
    const std::filesystem::path root = RootFor("drop_authored_new_game");
    DroppedItem first;
    DroppedItem second;
    bool afterAuthored = true;
    bool entryAfterAuthored = true;
    bool afterNewGame = true;
    int stage = 0;
    RunFps(kFirst.Name, 1500, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            first.Drop(engine, world, Vec3d{ 1.5f, 0.25f, -2.0f }, 3);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Authored);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            afterAuthored = Resolves(world, first.Pickup) || PickupsIn(world) > 0;
            entryAfterAuthored = Resolves(world, first.Entry);
            second.Drop(engine, world, Vec3d{ -1.0f, 0.25f, 2.0f }, 4);
            EXPECT_EQ(engine.LevelChanges().Request(LevelChangeRequest::NewGame(kFirst)), LevelChangeAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine))
        {
            afterNewGame = Resolves(world, second.Pickup) || PickupsIn(world) > 0;
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4);
    EXPECT_FALSE(afterAuthored) << "entering the level as authored forgets what it gained";
    EXPECT_FALSE(entryAfterAuthored) << "and the entry that waited for it";
    EXPECT_FALSE(afterNewGame) << "a new game forgets what every level gained";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ADroppedItemThatWasTakenIsNotBroughtBackByTheNextReturn)
{
    const std::filesystem::path root = RootFor("drop_taken");
    DroppedItem dropped;
    bool back = true;
    int stage = 0;
    RunFps(kFirst.Name, 1200, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            dropped.Drop(engine, world, Vec3d{ 1.5f, 0.25f, -2.0f }, 3);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 1;
        }
        else if (stage == 1 && Arrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            const EntityId body = LocalControlSubjectOf(world);
            if (!world.HasComponent<ItemContainer>(body))
                world.AddComponent<ItemContainer>(body, ItemContainer{});
            ASSERT_EQ(Inventory(world, FixedSimTime{}).Take(Live(world, dropped.Pickup), body).Refusal,
                      InventoryRefusal::None);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 4;
        }
        else if (stage == 4 && Arrived(engine))
        {
            back = Resolves(world, dropped.Pickup) || PickupsIn(world) > 0;
            stage = 5;
        }
    });
    ASSERT_EQ(stage, 5);
    EXPECT_FALSE(back) << "a taken item came back";
    std::filesystem::remove_all(root);
}

namespace
{
    const ZoneId kStartZone = *ZoneIdFromString("9c76e74ceb9abd59");
    const ZoneId kFarZone = *ZoneIdFromString("1ba7c9d40fec52ec");

    void StreamBriskly(EngineConfig& config)
    {
        config.Runtime.StreamingLingerSeconds = 0.0;
    }

    bool Resident(const Engine& engine, ZoneId zone)
    {
        const RuntimeZoneRecord* record = engine.World().FindZone(zone);
        return record != nullptr && record->State == RuntimeZoneLoadState::Resident;
    }

    bool WalkTowards(Engine& engine, World& world, float x)
    {
        const EntityId body = LocalControlSubjectOf(world);
        const Vec3d at = BodyPosition(world);
        if (std::abs(at.X - x) < 0.3f)
            return true;
        const float next = at.X + (x > at.X ? 0.25f : -0.25f);
        (void)engine.Schedule().Get<PhysicsStepSystem>()->GetCharacterMovers().SetPosition(
            world, body, Vec3d{ next, 1.0f, 0.0f });
        return false;
    }
}

TEST(SavedGames, AnItemDroppedInAStreamedZoneIsThereAfterTheZoneStreamsOutAndBack)
{
    const std::filesystem::path root = RootFor("drop_streamed");
    DroppedItem dropped;
    bool zoneLeft = false;
    bool back = false;
    int stage = 0;
    RunFps(
        kFirst.Name, 4000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone))
            {
                dropped.Drop(engine, world, Vec3d{ -2.0f, 0.25f, 3.0f }, 5);
                EXPECT_EQ(world.GetEntityPartition(Live(world, dropped.Pickup)),
                          engine.World().FindZone(kStartZone)->Partition);
                stage = 2;
            }
            else if (stage == 2 && WalkTowards(engine, world, 32.0f) && !Resident(engine, kStartZone))
            {
                zoneLeft = !Resolves(world, dropped.Pickup) && engine.World().FindZone(kStartZone) == nullptr;
                stage = 3;
            }
            else if (stage == 3 && WalkTowards(engine, world, 0.0f) && Resident(engine, kStartZone)
                     && Resolves(world, dropped.Pickup))
            {
                back = dropped.LiesWhereItFell(world, 5);
                stage = 4;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 4) << "the walk did not stream the zone out and back";
    EXPECT_TRUE(zoneLeft);
    EXPECT_TRUE(back) << "the dropped item did not come back with its zone";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AnItemDroppedInAStreamedZoneComesBackInAFreshRunWhetherOrNotItsZoneWasLoadedAtTheWrite)
{
    const std::filesystem::path root = RootFor("drop_streamed_saves");
    DroppedItem dropped;
    int stage = 0;
    RunFps(
        kFirst.Name, 4000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone))
            {
                dropped.Drop(engine, world, Vec3d{ -2.0f, 0.25f, 3.0f }, 5);
                EXPECT_EQ(engine.Saves().RequestWrite("loaded"), SaveAdmission::Accepted);
                stage = 2;
            }
            else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
            {
                stage = 3;
            }
            else if (stage == 3 && WalkTowards(engine, world, 32.0f) && !Resident(engine, kStartZone))
            {
                EXPECT_EQ(engine.Saves().RequestWrite("away"), SaveAdmission::Accepted);
                stage = 4;
            }
            else if (stage == 4 && engine.Saves().WriteStatus() == SaveStatus::Done)
            {
                stage = 5;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 5);
    for (const std::string_view slot : { "loaded", "away" })
    {
        const std::optional<SavedGame> saved = ReadSave(SlotFile(root, slot));
        ASSERT_TRUE(saved.has_value()) << slot;
        const DurableLevelState* world = LevelIn(*saved, kWorld);
        ASSERT_NE(world, nullptr) << slot;
        const auto zone = std::ranges::find(world->Zones, kStartZone, &DurableZoneState::Zone);
        ASSERT_NE(zone, world->Zones.end()) << slot;
        ASSERT_EQ(GainedIn(*zone).size(), 1u) << slot;
        ASSERT_EQ(ZoneEntities(zone->Held, kHeldZoneList).size(), 1u) << slot;
    }

    for (const std::string_view slot : { "loaded", "away" })
    {
        bool back = false;
        stage = 0;
        RunFps(
            kFirst.Name, 4000, root,
            [&](Engine& engine, World& world, int) {
                if (stage == 0 && HasBody(world))
                {
                    (void)DefineArrows(world);
                    EXPECT_EQ(engine.Saves().RequestRestore(std::string(slot)), SaveAdmission::Accepted);
                    stage = 1;
                }
                else if (stage == 1 && RestoreArrived(engine))
                {
                    stage = 2;
                }
                else if (stage == 2 && WalkTowards(engine, world, 0.0f) && Resident(engine, kStartZone)
                         && Resolves(world, dropped.Pickup))
                {
                    back = dropped.LiesWhereItFell(world, 5);
                    stage = 3;
                }
            },
            60.0, StreamBriskly);
        EXPECT_EQ(stage, 3) << slot;
        EXPECT_TRUE(back) << "the item dropped in the streamed zone did not come back from the save '" << slot << "'";
    }
    std::filesystem::remove_all(root);
}

namespace
{
    SavedGameThumbnail Picture()
    {
        SavedGameThumbnail thumbnail{ .Width = 4, .Height = 2, .Encoding = "rgba8", .Bytes = {} };
        for (int i = 0; i < 32; ++i)
            thumbnail.Bytes.push_back(static_cast<std::byte>(255 - i * 5));
        return thumbnail;
    }

    const char* const kGameMember =
        R"({"area":"Atrium","play_seconds":5400,"difficulty":"hard","marks":[1,"two",true,null,{"deep":-2.5}]})";

    bool HasChunk(const std::filesystem::path& path, std::uint32_t id)
    {
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(path, bytes))
            return false;
        for (std::size_t offset = 8; offset + 12 <= bytes.size();)
        {
            std::uint32_t chunk = 0;
            std::uint32_t size = 0;
            std::memcpy(&chunk, bytes.data() + offset, 4);
            std::memcpy(&size, bytes.data() + offset + 8, 4);
            if (chunk == id)
                return true;
            offset += 12 + size;
        }
        return false;
    }

    const SaveSlotSummary* Listed(const std::vector<SaveSlotSummary>& slots, std::string_view slot)
    {
        for (const SaveSlotSummary& listed : slots)
            if (listed.Slot == slot)
                return &listed;
        return nullptr;
    }

    std::optional<std::vector<std::byte>> BytesOf(const std::filesystem::path& path)
    {
        std::vector<std::byte> bytes;
        if (path.empty() || !ReadFileBytes(path, bytes))
            return std::nullopt;
        return bytes;
    }
}

TEST(SavedGames, TheGameMemberAndAThumbnailGivenAtCaptureComeBackUntouchedAndASaveWithoutOneHasNoChunk)
{
    const std::filesystem::path root = RootFor("summary_and_thumbnail");
    const JsonValue game = *JsonParse(kGameMember);
    std::optional<std::vector<SaveSlotSummary>> listed;
    std::optional<std::optional<SavedGameThumbnail>> pictured;
    std::optional<std::optional<SavedGameThumbnail>> plain;
    bool answeredLater = true;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            saves.SetSummary({ .Describe = [&](const World&) { return game; },
                               .Thumbnail = [](const World&) { return std::optional<SavedGameThumbnail>(Picture()); } });
            EXPECT_EQ(saves.RequestWrite("pictured"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            saves.SetSummary({ .Describe = [&](const World&) { return game; },
                               .Thumbnail = [](const World&) { return std::optional<SavedGameThumbnail>(); } });
            EXPECT_EQ(saves.RequestWrite("plain"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            saves.RequestList([&](std::vector<SaveSlotSummary> slots) { listed = std::move(slots); });
            saves.RequestThumbnail("pictured", [&](std::optional<SavedGameThumbnail> picture) { pictured = std::move(picture); });
            saves.RequestThumbnail("plain", [&](std::optional<SavedGameThumbnail> picture) { plain = std::move(picture); });
            answeredLater = !listed.has_value() && !pictured.has_value() && !plain.has_value();
            stage = 3;
        }
        else if (stage == 3 && listed.has_value() && pictured.has_value() && plain.has_value())
        {
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4) << "the two saves, or the listing and the thumbnails, did not all arrive";
    EXPECT_TRUE(answeredLater) << "a listing answers on the owner thread at a later drain, not inside the request";
    const SaveSlotSummary* withPicture = Listed(*listed, "pictured");
    const SaveSlotSummary* without = Listed(*listed, "plain");
    ASSERT_NE(withPicture, nullptr);
    ASSERT_NE(without, nullptr);
    EXPECT_EQ(JsonStringify(withPicture->Summary.Game), JsonStringify(game));
    EXPECT_EQ(JsonStringify(without->Summary.Game), JsonStringify(game));
    EXPECT_TRUE(withPicture->HasThumbnail);
    EXPECT_FALSE(without->HasThumbnail);
    EXPECT_EQ(*pictured, Picture());
    EXPECT_FALSE(plain->has_value());
    EXPECT_TRUE(HasChunk(SlotFile(root, "pictured"), MakeFourCC('T', 'H', 'M', 'B')));
    EXPECT_FALSE(HasChunk(SlotFile(root, "plain"), MakeFourCC('T', 'H', 'M', 'B')));
    const std::optional<SavedGame> read = ReadSave(SlotFile(root, "pictured"));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(JsonStringify(read->Summary.Game), JsonStringify(game));
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ADeletedSlotNeitherListsNorRestoresEvenInTheSessionThatWroteIt)
{
    const std::filesystem::path root = RootFor("deleted");
    std::filesystem::path file;
    bool hadBackup = false;
    bool remembered = true;
    bool held = true;
    std::optional<std::vector<SaveSlotSummary>> listed;
    std::string failure;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("doomed"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(saves.RequestWrite("doomed"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            file = SlotFile(root, "doomed");
            hadBackup = std::filesystem::exists(BackupOf(file));
            EXPECT_EQ(saves.RequestDelete("doomed"), SaveAdmission::Accepted);
            remembered = saves.Remembers("doomed");
            held = saves.Holds("doomed");
            saves.RequestList([&](std::vector<SaveSlotSummary> slots) { listed = std::move(slots); });
            EXPECT_EQ(saves.RequestRestore("doomed"), SaveAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 3 && saves.RestoreStatus() == RestoreProgress::Failed && listed.has_value())
        {
            failure = saves.RestoreFailure();
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4) << "the restore of a deleted slot did not fail";
    ASSERT_TRUE(hadBackup);
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_FALSE(std::filesystem::exists(BackupOf(file))) << "the backup did not go with its slot";
    EXPECT_FALSE(remembered) << "a deleted slot is not one this session can return to";
    EXPECT_FALSE(held);
    EXPECT_EQ(Listed(*listed, "doomed"), nullptr);
    EXPECT_NE(failure.find("doomed"), std::string::npos) << failure;
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ADeletedSlotDoesNotComeBackFromTheSavesMemory)
{
    bool heldBefore = false;
    bool held = true;
    bool remembered = true;
    std::optional<std::vector<SaveSlotSummary>> listed;
    std::string failure;
    int stage = 0;
    RunFps("levels/bumper", 400, {}, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("held"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            heldBefore = saves.Holds("held");
            EXPECT_EQ(saves.RequestDelete("held"), SaveAdmission::Accepted);
            held = saves.Holds("held");
            remembered = saves.Remembers("held");
            saves.RequestList([&](std::vector<SaveSlotSummary> slots) { listed = std::move(slots); });
            EXPECT_EQ(saves.RequestRestore("held"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.RestoreStatus() == RestoreProgress::Failed && listed.has_value())
        {
            failure = saves.RestoreFailure();
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << "the restore of a deleted slot held only in memory did not fail";
    EXPECT_TRUE(heldBefore);
    EXPECT_FALSE(held);
    EXPECT_FALSE(remembered);
    EXPECT_TRUE(listed->empty());
    EXPECT_NE(failure.find("held"), std::string::npos) << failure;
}

TEST(SavedGames, ADeleteOrACopyDuringThatSlotsWriteIsBusy)
{
    const std::filesystem::path root = RootFor("busy_slot");
    SaveAdmission waiting = SaveAdmission::Accepted;
    SaveAdmission elsewhere = SaveAdmission::Busy;
    SaveAdmission badName = SaveAdmission::Accepted;
    SaveAdmission working = SaveAdmission::Accepted;
    SaveAdmission copyOnto = SaveAdmission::Accepted;
    SaveStatus seen = SaveStatus::Idle;
    SaveAdmission after = SaveAdmission::Busy;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("busy"), SaveAdmission::Accepted);
            waiting = saves.RequestDelete("busy");
            elsewhere = saves.RequestDelete("elsewhere");
            badName = saves.RequestDelete("../busy");
            stage = 1;
        }
        else if (stage == 1)
        {
            seen = saves.WriteStatus();
            working = saves.RequestDelete("busy");
            copyOnto = saves.RequestCopy("elsewhere", "busy");
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            after = saves.RequestDelete("busy");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(waiting, SaveAdmission::Busy) << "a delete while the slot's write waits";
    EXPECT_EQ(elsewhere, SaveAdmission::Accepted) << "another slot's delete is not held up by this slot's write";
    EXPECT_EQ(badName, SaveAdmission::BadSlot);
    EXPECT_EQ(seen, SaveStatus::Working);
    EXPECT_EQ(working, SaveAdmission::Busy) << "a delete while the slot's write is on its way to disk";
    EXPECT_EQ(copyOnto, SaveAdmission::Busy);
    EXPECT_EQ(after, SaveAdmission::Accepted);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ACopyRestoresToTheSameWorldAsItsSourceAndTakesItsBackup)
{
    const std::filesystem::path root = RootFor("copied");
    PersistentEntityId lostBefore;
    PersistentEntityId lostAfter;
    bool lostBeforeStaysLost = false;
    bool lostAfterIsBack = false;
    bool remembered = false;
    SaveAdmission restoreDuringCopy = SaveAdmission::Accepted;
    int stage = 0;
    RunFps("levels/bumper", 600, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("source"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            lostBefore = DestroyAuthored(world);
            EXPECT_EQ(saves.RequestWrite("source"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(saves.RequestCopy("source", "copy"), SaveAdmission::Accepted);
            restoreDuringCopy = saves.RequestRestore("copy");
            stage = 3;
        }
        else if (stage == 3 && saves.WriteStatus() == SaveStatus::Done)
        {
            remembered = saves.Remembers("copy");
            lostAfter = DestroyAuthored(world, lostBefore);
            EXPECT_EQ(saves.RequestRestore("copy"), SaveAdmission::Accepted);
            stage = 4;
        }
        else if (stage == 4 && RestoreArrived(engine))
        {
            lostBeforeStaysLost = !Resolves(world, lostBefore);
            lostAfterIsBack = Resolves(world, lostAfter);
            stage = 5;
        }
    });
    ASSERT_EQ(stage, 5) << "the copy, or the restore of it, did not finish";
    EXPECT_EQ(restoreDuringCopy, SaveAdmission::Busy) << "a restore of a slot while a copy writes it";
    EXPECT_TRUE(lostBeforeStaysLost);
    EXPECT_TRUE(lostAfterIsBack) << "the copy did not restore the world its source saved";
    EXPECT_TRUE(remembered) << "a copy of a save this session wrote is one this session wrote";
    const std::filesystem::path source = SlotFile(root, "source");
    const std::filesystem::path copy = SlotFile(root, "copy");
    ASSERT_TRUE(BytesOf(copy).has_value());
    EXPECT_EQ(BytesOf(copy), BytesOf(source));
    ASSERT_TRUE(BytesOf(BackupOf(copy)).has_value()) << "the backup did not go with the copy";
    EXPECT_EQ(BytesOf(BackupOf(copy)), BytesOf(BackupOf(source)));

    bool rememberedInALaterRun = true;
    bool heldInALaterRun = false;
    stage = 0;
    RunFps("levels/bumper", 300, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestCopy("source", "copy"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            rememberedInALaterRun = saves.Remembers("copy");
            heldInALaterRun = saves.Holds("copy");
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_FALSE(rememberedInALaterRun) << "a copy of an earlier run's file is not this session's save";
    EXPECT_TRUE(heldInALaterRun);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, TheDeleteAndCopyVerbsAnswerWhyTheyRefuse)
{
    const std::filesystem::path root = RootFor("slot_verbs");
    std::string copied;
    std::string badTarget;
    std::string deletedDuringCopy;
    std::string deleted;
    bool heldAfterCopy = false;
    bool heldAfterDelete = true;
    int stage = 0;
    RunFps("levels/bumper", 400, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("verb_source"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            badTarget = TextOf(RunVerb(engine, { "save.copy", "from=verb_source", "to=../escape" }));
            copied = TextOf(RunVerb(engine, { "save.copy", "from=verb_source", "to=verb_copy" }));
            deletedDuringCopy = TextOf(RunVerb(engine, { "save.delete", "slot=verb_copy" }));
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            heldAfterCopy = saves.Holds("verb_copy");
            deleted = TextOf(RunVerb(engine, { "save.delete", "slot=verb_copy" }));
            heldAfterDelete = saves.Holds("verb_copy");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << copied;
    EXPECT_NE(badTarget.find("invalid argument 'to'"), std::string::npos) << badTarget;
    EXPECT_NE(copied.find("accepted"), std::string::npos) << copied;
    EXPECT_NE(deletedDuringCopy.find("refused: busy"), std::string::npos) << deletedDuringCopy;
    EXPECT_NE(deleted.find("accepted"), std::string::npos) << deleted;
    EXPECT_TRUE(heldAfterCopy);
    EXPECT_FALSE(heldAfterDelete);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ADeleteDuringThatSlotsRestoreIsBusyAndTheSlotIsGoneOnceItIsAccepted)
{
    const std::filesystem::path root = RootFor("delete_during_restore");
    SaveAdmission whilePending = SaveAdmission::Accepted;
    SaveAdmission elsewhere = SaveAdmission::Busy;
    std::vector<SaveAdmission> inFlight;
    SaveAdmission afterArrival = SaveAdmission::Busy;
    bool remembered = true;
    bool held = true;
    std::optional<std::vector<SaveSlotSummary>> listed;
    std::string failure;
    int stage = 0;
    RunFps("levels/bumper", 600, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(saves.RequestWrite("restored"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(saves.RequestWrite("elsewhere"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(saves.RequestRestore("restored"), SaveAdmission::Accepted);
            whilePending = saves.RequestDelete("restored");
            elsewhere = saves.RequestDelete("elsewhere");
            stage = 3;
        }
        else if (stage == 3 && !RestoreArrived(engine))
        {
            if (saves.RestoreStatus() == RestoreProgress::Reading || saves.RestoreStatus() == RestoreProgress::Loading)
                inFlight.push_back(saves.RequestDelete("restored"));
        }
        else if (stage == 3)
        {
            afterArrival = saves.RequestDelete("restored");
            remembered = saves.Remembers("restored");
            held = saves.Holds("restored");
            saves.RequestList([&](std::vector<SaveSlotSummary> slots) { listed = std::move(slots); });
            EXPECT_EQ(saves.RequestRestore("restored"), SaveAdmission::Accepted);
            stage = 4;
        }
        else if (stage == 4 && saves.RestoreStatus() == RestoreProgress::Failed && listed.has_value())
        {
            failure = saves.RestoreFailure();
            stage = 5;
        }
    });
    EXPECT_EQ(whilePending, SaveAdmission::Busy) << "a delete while that slot's restore waits";
    EXPECT_EQ(elsewhere, SaveAdmission::Accepted) << "another slot's delete is not held up by the restore";
    ASSERT_EQ(stage, 5) << "the restore, or the restore after the delete, did not finish";
    ASSERT_FALSE(inFlight.empty()) << "the restore was never seen reading or loading";
    for (const SaveAdmission admission : inFlight)
        EXPECT_EQ(admission, SaveAdmission::Busy) << "a delete while that slot's restore is under way";
    EXPECT_EQ(afterArrival, SaveAdmission::Accepted);
    EXPECT_FALSE(remembered);
    EXPECT_FALSE(held);
    EXPECT_EQ(Listed(*listed, "restored"), nullptr);
    EXPECT_NE(failure.find("restored"), std::string::npos) << failure;
    std::filesystem::remove_all(root);
}

namespace
{
    bool WaitForTheLaneToPass(Engine& engine)
    {
        auto passed = std::make_shared<std::atomic<bool>>(false);
        (void)engine.Tasks().Submit<int>(
            [passed] {
                passed->store(true);
                return 0;
            },
            [](int) {});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!passed->load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return passed->load();
    }
}

TEST(SavedGames, AListingOrAThumbnailAskedForBeforeADeleteDoesNotAnswerWithTheDeletedSlot)
{
    const std::filesystem::path root = RootFor("listing_before_delete");
    std::optional<std::vector<SaveSlotSummary>> listed;
    std::optional<std::optional<SavedGameThumbnail>> pictured;
    bool readBeforeTheDelete = false;
    int stage = 0;
    RunFps("levels/bumper", 480, root, [&](Engine& engine, World& world, int) {
        SavedGames& saves = engine.Saves();
        if (stage == 0 && HasBody(world))
        {
            saves.SetSummary({ .Describe = {},
                               .Thumbnail = [](const World&) { return std::optional<SavedGameThumbnail>(Picture()); } });
            EXPECT_EQ(saves.RequestWrite("fleeting"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && saves.WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(saves.RequestWrite("staying"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && saves.WriteStatus() == SaveStatus::Done)
        {
            saves.RequestList([&](std::vector<SaveSlotSummary> slots) { listed = std::move(slots); });
            saves.RequestThumbnail("fleeting", [&](std::optional<SavedGameThumbnail> picture) { pictured = std::move(picture); });
            readBeforeTheDelete = WaitForTheLaneToPass(engine) && !listed.has_value() && !pictured.has_value();
            EXPECT_EQ(saves.RequestDelete("fleeting"), SaveAdmission::Accepted);
            stage = 3;
        }
        else if (stage == 3 && listed.has_value() && pictured.has_value())
        {
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4) << "the listing and the thumbnail did not both arrive";
    ASSERT_TRUE(readBeforeTheDelete) << "the slot files were not read before the delete, so this proves nothing";
    EXPECT_EQ(Listed(*listed, "fleeting"), nullptr) << "a listing answered with a slot deleted since it was asked for";
    EXPECT_NE(Listed(*listed, "staying"), nullptr);
    EXPECT_FALSE(pictured->has_value()) << "a thumbnail answered for a slot deleted since it was asked for";
    std::filesystem::remove_all(root);
}

namespace
{
    const PersistentEntityId kCrate = *PersistentEntityIdFromString("6f2b1c0d9e8a7c01");
    const PersistentEntityId kStackBottom = *PersistentEntityIdFromString("6f2b1c0d9e8a7c02");
    const PersistentEntityId kStackTop = *PersistentEntityIdFromString("6f2b1c0d9e8a7c03");
    const PersistentEntityId kZoneCrate = *PersistentEntityIdFromString("5882e5a3d55e9c01");

    struct CrateState
    {
        Vec3d At;
        Vec3d Velocity;
        bool Asleep = false;
    };

    std::optional<CrateState> CrateIn(const World& world, PersistentEntityId id)
    {
        const EntityId crate = Live(world, id);
        if (!crate.IsValid())
            return std::nullopt;
        const LocalTransform* placed = world.TryGet<LocalTransform>(crate);
        const RigidBody* body = world.TryGet<RigidBody>(crate);
        if (placed == nullptr || body == nullptr)
            return std::nullopt;
        return CrateState{ .At = placed->Value.Position, .Velocity = body->LinearVelocity, .Asleep = body->Asleep };
    }

    bool Push(Engine& engine, const World& world, PersistentEntityId id, Vec3d velocity)
    {
        const EntityId crate = Live(world, id);
        const PhysicsBodyLink* link = crate.IsValid() ? world.TryGet<PhysicsBodyLink>(crate) : nullptr;
        if (link == nullptr)
            return false;
        PhysicsWorld& simulation = engine.Schedule().Get<PhysicsStepSystem>()->GetSimulation();
        simulation.SetLinearVelocity(link->Body, velocity);
        simulation.WakeBody(link->Body);
        return true;
    }

    bool SettledAway(const World& world, PersistentEntityId id, Vec3d authored)
    {
        const std::optional<CrateState> crate = CrateIn(world, id);
        return crate.has_value() && crate->Asleep && (crate->At - authored).Magnitude() > 0.5f;
    }

    float Apart(Vec3d a, Vec3d b) { return (a - b).Magnitude(); }

    std::optional<JsonValue> ChangedPatchOf(const SavedGame& saved, const LevelReference& level, PersistentEntityId id)
    {
        if (const DurableLevelState* state = LevelIn(saved, level))
            for (const DurableZoneState& zone : state->Zones)
                for (ZoneListEntity& entity : ZoneEntities(zone.Lists, kChangedZoneList))
                    if (entity.Id == id)
                        return std::move(entity.Value);
        return std::nullopt;
    }
}

TEST(SavedGames, APushedCrateIsWhereItWasLeftAfterItsStreamedZoneLeavesAndComesBack)
{
    const std::filesystem::path root = RootFor("crate_streamed");
    std::optional<CrateState> left;
    std::optional<CrateState> back;
    std::optional<CrateState> later;
    int stage = 0;
    int waited = 0;
    RunFps(
        kFirst.Name, 5000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone)
                     && Push(engine, world, kZoneCrate, Vec3d{ 3.0f, 0.0f, 2.0f }))
            {
                stage = 2;
            }
            else if (stage == 2 && SettledAway(world, kZoneCrate, Vec3d{ -4.0f, 0.5f, -4.0f }))
            {
                left = CrateIn(world, kZoneCrate);
                stage = 3;
            }
            else if (stage == 3 && WalkTowards(engine, world, 32.0f) && !Resident(engine, kStartZone))
            {
                stage = 4;
            }
            else if (stage == 4 && WalkTowards(engine, world, 0.0f) && Resident(engine, kStartZone)
                     && Resolves(world, kZoneCrate))
            {
                back = CrateIn(world, kZoneCrate);
                stage = 5;
            }
            else if (stage == 5 && ++waited == 120)
            {
                later = CrateIn(world, kZoneCrate);
                stage = 6;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 6) << "the crate was not pushed and the zone not streamed out and back";
    ASSERT_TRUE(left.has_value() && back.has_value() && later.has_value());
    EXPECT_LT(Apart(back->At, left->At), 1e-3f) << "the crate went back to where the zone authored it";
    EXPECT_TRUE(back->Asleep) << "a crate left asleep came back awake";
    EXPECT_LT(Apart(later->At, left->At), 1e-3f) << "the crate moved after its zone came back";
    std::filesystem::remove_all(root);
}

namespace
{
    constexpr std::string_view kLasting = "test.lasting";

    EffectId Lasting(World& world)
    {
        EffectDefinition definition;
        definition.Duration = EffectDuration::Duration;
        definition.DurationSeconds = 120.0f;
        return world.GetResource<EffectRegistry>().Register(kLasting, definition);
    }

    std::optional<std::uint32_t> LastingLeftOn(const World& world, EntityId target)
    {
        std::optional<std::uint32_t> left;
        int count = 0;
        for (const EntityId entity : world.GetAliveEntities())
        {
            const ActiveEffect* active = world.TryGet<ActiveEffect>(entity);
            const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
            if (active != nullptr && active->Target == target && effects != nullptr
                && effects->GetName(active->Def) == kLasting)
            {
                left = active->RemainingTicks;
                ++count;
            }
        }
        return count == 1 ? left : std::nullopt;
    }

    std::size_t RowCarriers(const World& world)
    {
        std::size_t carriers = 0;
        for (const EntityId entity : world.GetAliveEntities())
            carriers += world.HasComponent<AppliedEffects>(entity) ? 1 : 0;
        return carriers;
    }
}

TEST(SavedGames, ALastingEffectOnTheBodyAndOnALevelEntityKeepsItsTimeAcrossAZoneRoundTripAndASaveInAFreshRun)
{
    const std::filesystem::path root = RootFor("lasting_effects");
    std::optional<std::uint32_t> appliedCrate;
    std::optional<std::uint32_t> crateBack;
    std::optional<std::uint32_t> savedBody;
    std::optional<std::uint32_t> savedCrate;
    std::size_t carriers = 0;
    int stage = 0;
    int waited = 0;
    RunFps(
        kFirst.Name, 6000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone) && Resolves(world, kZoneCrate))
            {
                const FixedSimTime now = engine.Runtime().GetSimulationClock().TimeOfTick(0);
                const EffectId lasting = Lasting(world);
                (void)ApplyEffect(world, LocalControlSubjectOf(world), lasting, now);
                (void)ApplyEffect(world, world.GetResource<PersistentEntityIndex>().TryResolve(kZoneCrate), lasting, now);
                appliedCrate = LastingLeftOn(world, world.GetResource<PersistentEntityIndex>().TryResolve(kZoneCrate));
                stage = 2;
            }
            else if (stage == 2 && WalkTowards(engine, world, 32.0f) && !Resident(engine, kStartZone))
            {
                stage = 3;
            }
            else if (stage == 3 && WalkTowards(engine, world, 0.0f) && Resident(engine, kStartZone)
                     && Resolves(world, kZoneCrate))
            {
                stage = 4;
            }
            else if (stage == 4 && ++waited == 30)
            {
                crateBack = LastingLeftOn(world, world.GetResource<PersistentEntityIndex>().TryResolve(kZoneCrate));
                stage = 5;
            }
            else if (stage == 5)
            {
                EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
                savedBody = LastingLeftOn(world, LocalControlSubjectOf(world));
                savedCrate = LastingLeftOn(world, world.GetResource<PersistentEntityIndex>().TryResolve(kZoneCrate));
                stage = 6;
            }
            else if (stage == 6 && engine.Saves().WriteStatus() == SaveStatus::Done)
            {
                carriers = RowCarriers(world);
                stage = 7;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 7) << "the crate's zone did not stream out and back";
    ASSERT_TRUE(appliedCrate.has_value());
    ASSERT_TRUE(crateBack.has_value()) << "the crate's lasting effect did not come back once with its zone";
    ASSERT_TRUE(savedBody.has_value() && savedCrate.has_value());
    EXPECT_LE(*crateBack, *appliedCrate);
    EXPECT_GT(*crateBack, *appliedCrate - 600) << "the crate's effect lost or regained time across its zone's round trip";
    EXPECT_EQ(carriers, 2u) << "an entity never under a kept effect at a capture carries the row";

    std::optional<std::uint32_t> restoredBody;
    std::optional<std::uint32_t> restoredCrate;
    std::uint64_t restoredTick = 0;
    stage = 0;
    RunFps(
        kFirst.Name, 6000, root,
        [&](Engine& engine, World& world, int frame) {
            if (stage == 0 && HasBody(world) && frame > 200)
            {
                (void)Lasting(world);
                EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
                stage = 1;
            }
            else if (stage == 1 && RestoreArrived(engine) && Resolves(world, kZoneCrate))
            {
                restoredTick = engine.Runtime().GetSimulationClock().GetTickIndex();
                stage = 2;
            }
            else if (stage == 2 && engine.Runtime().GetSimulationClock().GetTickIndex() > restoredTick)
            {
                restoredBody = LastingLeftOn(world, LocalControlSubjectOf(world));
                restoredCrate = LastingLeftOn(world, world.GetResource<PersistentEntityIndex>().TryResolve(kZoneCrate));
                stage = 3;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 3);
    ASSERT_TRUE(restoredBody.has_value()) << "the body's lasting effect did not come back once";
    ASSERT_TRUE(restoredCrate.has_value()) << "the crate's lasting effect did not come back once";
    EXPECT_LE(*restoredBody, *savedBody);
    EXPECT_GT(*restoredBody, *savedBody - 30) << "the body's effect came back with another time left";
    EXPECT_LE(*restoredCrate, *savedCrate);
    EXPECT_GT(*restoredCrate, *savedCrate - 30) << "the crate's effect came back with another time left";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, AWriteOfAnEntityUnderMoreKeptEffectsThanASaveKeepsFailsNamingItInADevelopmentBuild)
{
#ifdef NDEBUG
    GTEST_SKIP() << "a release build leaves out the shortest and logs it";
#endif
    const std::filesystem::path root = RootFor("overfull_effects");
    std::string failure;
    int stage = 0;
    RunFps(kFirst.Name, 600, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            const FixedSimTime now = engine.Runtime().GetSimulationClock().TimeOfTick(0);
            for (int i = 0; i <= kAppliedEffectCapacity; ++i)
            {
                EffectDefinition definition;
                definition.Duration = EffectDuration::Duration;
                definition.DurationSeconds = 60.0f;
                (void)ApplyEffect(world, LocalControlSubjectOf(world),
                                  world.GetResource<EffectRegistry>().Register("test.many." + std::to_string(i), definition),
                                  now);
            }
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Failed)
        {
            failure = engine.Saves().WriteFailure();
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2) << "a write with a row it cannot hold did not fail";
    EXPECT_NE(failure.find("kept effects"), std::string::npos) << failure;
    EXPECT_FALSE(std::filesystem::exists(SlotFile(root, "default")));
    std::filesystem::remove_all(root);
}

TEST(SavedGames, APushedCrateIsWhereItWasLeftAfterAJourneyThereAndBack)
{
    const std::filesystem::path root = RootFor("crate_journey");
    std::optional<CrateState> left;
    std::optional<CrateState> back;
    int stage = 0;
    RunFps(kFirst.Name, 3000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world) && Push(engine, world, kCrate, Vec3d{ -3.0f, 0.0f, 2.0f }))
        {
            stage = 1;
        }
        else if (stage == 1 && SettledAway(world, kCrate, Vec3d{ 6.0f, 0.5f, -6.0f }))
        {
            left = CrateIn(world, kCrate);
            Travel(engine, kSecond, LevelMemory::Remembered);
            stage = 2;
        }
        else if (stage == 2 && Arrived(engine))
        {
            Travel(engine, kFirst, LevelMemory::Remembered);
            stage = 3;
        }
        else if (stage == 3 && Arrived(engine) && Resolves(world, kCrate))
        {
            back = CrateIn(world, kCrate);
            stage = 4;
        }
    });
    ASSERT_EQ(stage, 4);
    ASSERT_TRUE(left.has_value() && back.has_value());
    EXPECT_LT(Apart(back->At, left->At), 1e-3f) << "the crate went back to where the level authored it";
    EXPECT_TRUE(back->Asleep);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ACrateComesBackFromASaveInAFreshRunWhereItWasAsleepIfItWasAndMovingIfItWas)
{
    const std::filesystem::path root = RootFor("crate_fresh_run");
    std::optional<CrateState> asleep;
    int stage = 0;
    RunFps(kFirst.Name, 3000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world) && Push(engine, world, kCrate, Vec3d{ -3.0f, 0.0f, 2.0f }))
        {
            stage = 1;
        }
        else if (stage == 1 && SettledAway(world, kCrate, Vec3d{ 6.0f, 0.5f, -6.0f }))
        {
            asleep = CrateIn(world, kCrate);
            EXPECT_EQ(engine.Saves().RequestWrite("asleep"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done
                 && Push(engine, world, kCrate, Vec3d{ 0.0f, 9.0f, 0.0f }))
        {
            stage = 3;
        }
        else if (stage == 3 && CrateIn(world, kCrate).has_value() && CrateIn(world, kCrate)->Velocity.Y > 4.0f)
        {
            EXPECT_EQ(engine.Saves().RequestWrite("moving"), SaveAdmission::Accepted);
            stage = 4;
        }
        else if (stage == 4 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 5;
        }
    });
    ASSERT_EQ(stage, 5);
    ASSERT_TRUE(asleep.has_value());

    const std::optional<SavedGame> movingSave = ReadSave(SlotFile(root, "moving"));
    ASSERT_TRUE(movingSave.has_value());
    const std::optional<JsonValue> patch = ChangedPatchOf(*movingSave, kFirst, kCrate);
    ASSERT_TRUE(patch.has_value()) << "the save holds nothing of the crate";
    EXPECT_NE(patch->Find("Transform"), nullptr) << "the save does not hold where the crate was";
    ASSERT_NE(patch->Find("RigidBody"), nullptr);
    ASSERT_NE(patch->Find("RigidBody")->Find("linear_velocity"), nullptr) << "the save does not hold how it moved";
    const JsonValue::Array& savedAt = patch->Find("Transform")->Find("local")->Find("position")->AsArray();
    const Vec3d saved{ static_cast<float>(savedAt[0].AsNumber()), static_cast<float>(savedAt[1].AsNumber()),
                       static_cast<float>(savedAt[2].AsNumber()) };

    std::optional<CrateState> restored;
    std::optional<CrateState> later;
    int waited = 0;
    stage = 0;
    RunFps(kFirst.Name, 3000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore("asleep"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            restored = CrateIn(world, kCrate);
            stage = 2;
        }
        else if (stage == 2 && ++waited == 120)
        {
            later = CrateIn(world, kCrate);
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    ASSERT_TRUE(restored.has_value() && later.has_value());
    EXPECT_LT(Apart(restored->At, asleep->At), 1e-3f) << "the crate did not come back where it was saved";
    EXPECT_TRUE(restored->Asleep) << "a crate saved asleep came back awake";
    EXPECT_LT(Apart(later->At, asleep->At), 1e-3f) << "the restored crate did not stay where it was (through the floor?)";

    EntityId authoredCrate;
    std::optional<Vec3d> first;
    float highest = -1000.0f;
    stage = 0;
    RunFps(kFirst.Name, 3000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            authoredCrate = Live(world, kCrate);
            EXPECT_EQ(engine.Saves().RequestRestore("moving"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && Live(world, kCrate).IsValid() && Live(world, kCrate) != authoredCrate
                 && CrateIn(world, kCrate).has_value() && world.HasComponent<PhysicsBodyLink>(Live(world, kCrate)))
        {
            const Vec3d at = CrateIn(world, kCrate)->At;
            if (!first.has_value())
                first = at;
            highest = std::max(highest, at.Y);
            if (highest > first->Y + 0.1f)
                stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    ASSERT_TRUE(first.has_value());
    EXPECT_NEAR(first->X, saved.X, 1e-3f) << "the crate saved in the air did not come back where it was";
    EXPECT_NEAR(first->Z, saved.Z, 1e-3f);
    EXPECT_GE(first->Y, saved.Y - 1e-3f);
    EXPECT_GT(highest, first->Y + 0.1f) << "the crate saved rising did not come back rising";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ARestingStackSavedAndLoadedFiveTimesMovesUnderAMillimetre)
{
    const std::filesystem::path root = RootFor("stack_five_loads");
    std::vector<std::pair<Vec3d, Vec3d>> saved;
    int stage = 0;
    int loads = 0;
    RunFps(kFirst.Name, 12000, root, [&](Engine& engine, World& world, int) {
        const auto settled = [&] {
            const std::optional<CrateState> bottom = CrateIn(world, kStackBottom);
            const std::optional<CrateState> top = CrateIn(world, kStackTop);
            return bottom.has_value() && top.has_value() && bottom->Asleep && top->Asleep;
        };
        if (stage == 0 && HasBody(world) && settled())
        {
            saved.emplace_back(CrateIn(world, kStackBottom)->At, CrateIn(world, kStackTop)->At);
            EXPECT_EQ(engine.Saves().RequestWrite("stack"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            EXPECT_EQ(engine.Saves().RequestRestore("stack"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && RestoreArrived(engine) && settled())
        {
            stage = ++loads == 5 ? 3 : 0;
            if (stage == 3)
                saved.emplace_back(CrateIn(world, kStackBottom)->At, CrateIn(world, kStackTop)->At);
        }
    });
    ASSERT_EQ(stage, 3) << "the stack was not saved and loaded five times";
    ASSERT_EQ(saved.size(), 6u);
    EXPECT_LT(Apart(saved.back().first, saved.front().first), 1e-3f) << "the stack's bottom crate crept";
    EXPECT_LT(Apart(saved.back().second, saved.front().second), 1e-3f) << "the stack's top crate crept";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ABodyHasAnIdFromItsAssignmentInAGameThatKeepsLevelStateAndARestoreGivesItBack)
{
    const std::filesystem::path root = RootFor("body_id_at_assignment");
    PersistentEntityId named;
    bool sameAfterWrite = false;
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            if (HasPersistentId(world, LocalControlSubjectOf(world)))
                named = world.TryGet<PersistentIdComponent>(LocalControlSubjectOf(world))->Id;
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            sameAfterWrite = HasPersistentId(world, LocalControlSubjectOf(world))
                && world.TryGet<PersistentIdComponent>(LocalControlSubjectOf(world))->Id == named;
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    ASSERT_TRUE(named.IsValid()) << "the body was given no id when it was assigned";
    EXPECT_FALSE(IsAuthoredPersistentEntityId(named));
    EXPECT_TRUE(sameAfterWrite) << "writing the save changed the body's id";
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const JsonValue* local = LocalBodyIn(*saved);
    ASSERT_NE(local, nullptr);
    ASSERT_NE(local->Find("id"), nullptr) << "the save does not name the body";

    PersistentEntityId restored;
    stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            if (HasPersistentId(world, LocalControlSubjectOf(world)))
                restored = world.TryGet<PersistentIdComponent>(LocalControlSubjectOf(world))->Id;
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_EQ(restored, named) << "the restored body did not take its saved id";
    std::filesystem::remove_all(root);
}

namespace
{
    const PersistentEntityId kFirstPlateCrate = *PersistentEntityIdFromString("5882e5a3d55e9d11");

    std::int32_t WorldCount(World& world, std::string_view name)
    {
        const std::optional<GameplayTagId> tag = world.GetResource<GameplayTagRegistry>().RegisterTag(name);
        return tag.has_value() ? CounterValue(world, *tag, EntityId{}) : -1;
    }

    int PlateEntries(const World& world)
    {
        const SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
        if (overlaps == nullptr)
            return 0;
        int entries = 0;
        for (const char* plate : { "5882e5a3d55e9d01", "5882e5a3d55e9d02", "5882e5a3d55e9d03" })
        {
            const EntityId sensor = Live(world, *PersistentEntityIdFromString(plate));
            for (const SensorOccurrence& occurrence : overlaps->Occurrences())
                entries += occurrence.Pair.Sensor == sensor && occurrence.Transition == SensorTransition::Enter ? 1 : 0;
        }
        return entries;
    }

    bool StandOn(Engine& engine, World& world, Vec3d at)
    {
        const EntityId body = LocalControlSubjectOf(world);
        if (!body.IsValid() || !engine.Schedule().Get<PhysicsStepSystem>()->GetCharacterMovers().SetPosition(world, body, at))
            return false;
        if (KinematicState* motion = world.TryGet<KinematicState>(body))
            motion->Velocity = Vec3d::Zero();
        return true;
    }
}

TEST(SavedGames, ThreeCratesOnThreeCountingPlatesReadThreeAfterTheirStreamedZonesRoundTrip)
{
    const std::filesystem::path root = RootFor("plates_streamed");
    std::int32_t first = -1;
    std::int32_t back = -1;
    int entered = 0;
    int stage = 0;
    int waited = 0;
    RunFps(
        kFirst.Name, 5000, root,
        [&](Engine& engine, World& world, int) {
            if (stage >= 3)
                entered += PlateEntries(world);
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone) && WorldCount(world, "test.plates") == 3
                     && ++waited == 60)
            {
                first = WorldCount(world, "test.plates");
                stage = 2;
            }
            else if (stage == 2 && WalkTowards(engine, world, 32.0f) && !Resident(engine, kStartZone))
            {
                stage = 3;
            }
            else if (stage == 3 && WalkTowards(engine, world, 0.0f) && Resident(engine, kStartZone))
            {
                waited = 0;
                stage = 4;
            }
            else if (stage == 4 && ++waited == 120)
            {
                back = WorldCount(world, "test.plates");
                stage = 5;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 5) << "the plates did not count their crates, or the zone did not stream out and back";
    EXPECT_EQ(first, 3);
    EXPECT_EQ(back, 3) << "the plates counted the crates already on them again when their zone came back";
    EXPECT_EQ(entered, 0) << "a plate reported a crate already on it as entering when its zone came back";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ThreeCratesOnThreeCountingPlatesReadThreeAfterALoadAndOneThatLeavesExits)
{
    const std::filesystem::path root = RootFor("plates_saved");
    int stage = 0;
    int waited = 0;
    RunFps(
        kFirst.Name, 4000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                Travel(engine, kWorld, LevelMemory::Authored);
                stage = 1;
            }
            else if (stage == 1 && Arrived(engine) && Resident(engine, kStartZone) && WorldCount(world, "test.plates") == 3
                     && ++waited == 60)
            {
                EXPECT_EQ(engine.Saves().RequestWrite("plates"), SaveAdmission::Accepted);
                stage = 2;
            }
            else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
            {
                stage = 3;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 3);

    std::int32_t loaded = -1;
    std::int32_t left = -1;
    std::int32_t counted = -1;
    stage = 0;
    waited = 0;
    RunFps(
        kFirst.Name, 8000, root,
        [&](Engine& engine, World& world, int) {
            if (stage == 0 && HasBody(world))
            {
                EXPECT_EQ(engine.Saves().RequestRestore("plates"), SaveAdmission::Accepted);
                stage = 1;
            }
            else if (stage == 1 && RestoreArrived(engine) && Resident(engine, kStartZone) && ++waited == 120)
            {
                loaded = WorldCount(world, "test.plates");
                stage = Push(engine, world, kFirstPlateCrate, Vec3d{ 0.0f, 0.0f, 6.0f }) ? 2 : 1;
                waited = 0;
            }
            else if (stage == 2 && (WorldCount(world, "test.left") > 0 || ++waited == 3000))
            {
                left = WorldCount(world, "test.left");
                counted = WorldCount(world, "test.plates");
                stage = 3;
            }
        },
        60.0, StreamBriskly);
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(loaded, 3) << "the load counted the crates already on the plates again";
    EXPECT_EQ(left, 1) << "a crate pushed off its plate after the load did not exit it";
    EXPECT_EQ(counted, 3);
    std::filesystem::remove_all(root);
}

TEST(SavedGames, APlayerSavedOnAPlateDoesNotPressItAgainThoughTheBodyLandsLater)
{
    const std::filesystem::path root = RootFor("plate_player");
    int stage = 0;
    RunFps(kFirst.Name, 4000, root, [&](Engine& engine, World& world, int) {
        if (stage > 0 && stage < 3)
            (void)StandOn(engine, world, Vec3d{ 3.0f, 1.0f, 3.0f });
        if (stage == 0 && HasBody(world))
        {
            stage = 1;
        }
        else if (stage == 1 && WorldCount(world, "test.pressed") == 1)
        {
            EXPECT_EQ(engine.Saves().RequestWrite("on_plate"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3) << "the player did not press the plate";

    std::int32_t pressed = -1;
    Vec3d stood;
    int waited = 0;
    stage = 0;
    RunFps(kFirst.Name, 2000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore("on_plate"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine) && ++waited == 120)
        {
            pressed = WorldCount(world, "test.pressed");
            stood = BodyPosition(world);
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_NEAR(stood.X, 3.0f, 0.2f) << "the body did not come back on the plate, so this tests nothing";
    EXPECT_NEAR(stood.Z, 3.0f, 0.2f);
    EXPECT_EQ(pressed, 1) << "the restored body pressed the plate it was saved standing on";
    std::filesystem::remove_all(root);
}

TEST(SavedGames, ABodyRestoredAwayFromThePlateItWasSavedOnPressesItWhenItStepsOnAgain)
{
    const std::filesystem::path root = RootFor("plate_arrival");
    int stage = 0;
    RunFps(kFirst.Name, 4000, root, [&](Engine& engine, World& world, int) {
        if (stage > 0 && stage < 3)
            (void)StandOn(engine, world, Vec3d{ 3.0f, 1.0f, 3.0f });
        if (stage == 0 && HasBody(world))
        {
            stage = 1;
        }
        else if (stage == 1 && WorldCount(world, "test.pressed") == 1)
        {
            EXPECT_EQ(engine.Saves().RequestWrite("arrival", "bumper_west"), SaveAdmission::Accepted);
            stage = 2;
        }
        else if (stage == 2 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);

    std::int32_t arrived = -1;
    std::int32_t pressedAgain = -1;
    int waited = 0;
    stage = 0;
    RunFps(kFirst.Name, 3000, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore("arrival"), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine) && ++waited == 120)
        {
            arrived = WorldCount(world, "test.pressed");
            waited = 0;
            stage = 2;
        }
        else if (stage == 2 && StandOn(engine, world, Vec3d{ 3.0f, 1.0f, 3.0f }) && ++waited == 120)
        {
            pressedAgain = WorldCount(world, "test.pressed");
            stage = 3;
        }
    });
    ASSERT_EQ(stage, 3);
    EXPECT_EQ(arrived, 1) << "a body that arrived at the landmark pressed the plate";
    EXPECT_EQ(pressedAgain, 2) << "stepping on the plate after the load did not press it";
    std::filesystem::remove_all(root);
}

#endif
