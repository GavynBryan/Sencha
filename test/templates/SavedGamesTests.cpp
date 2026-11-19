#include "TemplateModuleRun.h"

#include <app/LevelChange.h>
#include <core/console/ConsoleService.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/CounterVocabulary.h>
#include <participant/LocalControl.h>
#include <participant_spawn/RestoredBodies.h>
#include <save/SavedGame.h>
#include <save/SavedGames.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/transform/TransformComponents.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// SavedGames' contract, driven through the FPS template's engine and content:
// each case scripts frames of a headless run and reads the slot files after.
//=============================================================================
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

    void RunFps(std::string_view level, int frames, const std::filesystem::path& root, FrameScript script)
    {
        Script = std::move(script);
        {
            TemplateModuleRun<ScriptProbe> run(TEST_FPS_MODULE_PATH, SENCHA_REPO_ROOT "/templates/fps/assets", level,
                                               frames,
                                               [&root](EngineConfig& config) { config.Runtime.SavesRoot = root.string(); });
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
        return DecodeSavedGame(bytes);
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
        return engine.Saves().RestoreStatus() == SaveStatus::Done
            && engine.LevelChanges().Serial() == engine.Saves().RestoreChange()
            && engine.LevelChanges().Status() == LevelChangeStatus::BodiesRestored;
    }

    // Two saves in one run: the backup holds the first, where `second` is still there.
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

#endif
