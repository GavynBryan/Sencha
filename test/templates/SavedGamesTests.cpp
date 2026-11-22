#include "TemplateModuleRun.h"

#include <app/LevelChange.h>
#include <app/VocabularyCommands.h>
#include <core/console/ConsoleService.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/CounterVocabulary.h>
#include <movement/components/CharacterFacts.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/RestoredBodies.h>
#include <physics/PhysicsStepSystem.h>
#include <save/SavedGame.h>
#include <save/SavedGames.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneStateLists.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
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
                double tickRate = 60.0)
    {
        Script = std::move(script);
        {
            TemplateModuleRun<ScriptProbe> run(TEST_FPS_MODULE_PATH, SENCHA_REPO_ROOT "/templates/fps/assets", level,
                                               frames, [&root, tickRate](EngineConfig& config) {
                                                   config.Runtime.SavesRoot = root.string();
                                                   config.Runtime.FixedTickRate = tickRate;
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
    EXPECT_TRUE(has("  CharacterMovement: authored, follows its prefab"));
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

TEST(SavedGames, ABodyNothingReferencesIsSavedAndRestoredWithoutAnId)
{
    const std::filesystem::path root = RootFor("body_without_id");
    bool unnamedBefore = false;
    bool unnamedAfter = false;
    int stage = 0;
    RunFps("levels/room_2", 300, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            unnamedBefore = !HasPersistentId(world, LocalControlSubjectOf(world));
            EXPECT_EQ(engine.Saves().RequestWrite(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && engine.Saves().WriteStatus() == SaveStatus::Done)
        {
            unnamedAfter = !HasPersistentId(world, LocalControlSubjectOf(world));
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    ASSERT_TRUE(unnamedBefore) << "something referenced the body before the save";
    EXPECT_TRUE(unnamedAfter) << "writing the save gave the body an id";
    const std::optional<SavedGame> saved = ReadSave(SlotFile(root, "default"));
    ASSERT_TRUE(saved.has_value());
    const JsonValue* local = LocalBodyIn(*saved);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->Find("id"), nullptr);

    std::optional<bool> restoredUnnamed;
    stage = 0;
    RunFps("levels/room_2", 400, root, [&](Engine& engine, World& world, int) {
        if (stage == 0 && HasBody(world))
        {
            EXPECT_EQ(engine.Saves().RequestRestore(), SaveAdmission::Accepted);
            stage = 1;
        }
        else if (stage == 1 && RestoreArrived(engine))
        {
            restoredUnnamed = !HasPersistentId(world, LocalControlSubjectOf(world));
            stage = 2;
        }
    });
    ASSERT_EQ(stage, 2);
    EXPECT_EQ(restoredUnnamed, true);
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
#endif
