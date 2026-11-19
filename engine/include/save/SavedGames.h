#pragma once

#include <core/json/JsonValue.h>
#include <jobs/AsyncTaskQueue.h>
#include <save/SaveSlotFiles.h>
#include <save/SavedGame.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Engine;
class World;

inline constexpr std::string_view kDefaultSaveSlot = "default";

// Named data the engine or a game keeps in every save beside the world's own.
struct SavedGameSectionDefinition
{
    std::string Name;
    std::uint32_t Version = 1;
    std::function<JsonValue(World&)> Capture;
    // Once the restoring level change has installed zone state, before the level
    // arrives; given a section of Version or older.
    std::function<void(World&, std::uint32_t version, const JsonValue& data)> Restore;
};

enum class SaveAdmission : std::uint8_t
{
    Accepted,
    Busy,
    BadSlot,
    NothingLoaded,
    NotAuthority,
    InSession,
};

enum class SaveStatus : std::uint8_t
{
    Idle,
    Pending,
    Working,
    Done,
    Failed,
};

// Writing and restoring saves: docs/gameplay/saves.md. Both are requests
// carried out at the zone-residency phase, before the level change's.
class SavedGames
{
public:
    // An empty root keeps saves in memory for the session only.
    SavedGames(Engine& engine, std::filesystem::path root);
    ~SavedGames();

    SavedGames(const SavedGames&) = delete;
    SavedGames& operator=(const SavedGames&) = delete;

    // Refused for a name already registered.
    [[nodiscard]] bool RegisterSection(SavedGameSectionDefinition section);
    void UnregisterSection(std::string_view name);

    [[nodiscard]] SaveAdmission RequestWrite(std::string slot = std::string(kDefaultSaveSlot));
    [[nodiscard]] SaveAdmission RequestRestore(std::string slot = std::string(kDefaultSaveSlot));

    [[nodiscard]] SaveStatus WriteStatus() const { return Writing; }
    [[nodiscard]] SaveStatus RestoreStatus() const { return Restoring; }
    // The LevelChange serial the latest restore asked for, to wait on its bodies.
    [[nodiscard]] std::uint64_t RestoreChange() const { return RestoreSerial; }
    [[nodiscard]] const std::string& Failure() const { return Reason; }
    // The latest restore read the slot's backup because its file did not read.
    [[nodiscard]] bool RestoredFromBackup() const { return FromBackup; }
    // What a restore of the slot would find, in this session's memory or on
    // disk; reads and decodes the slot's files when this session has not.
    [[nodiscard]] bool Holds(std::string_view slot) const;

    void Commit();
    // After the level change commits: a restore's sections are put back.
    void RestoreSections();
    // At shutdown, before the async lane stops: a write requested and not yet
    // on disk is written on this thread.
    void Finish();

private:
    struct InFlightWrite
    {
        std::string Slot;
        std::shared_ptr<const SavedGame> Saved;
        AsyncTaskHandle Task;
    };

    // Nothing when the level is ready to capture; empty while it is changing.
    [[nodiscard]] std::optional<std::string> WriteRefusal() const;
    // Null when there is nothing to write to disk.
    [[nodiscard]] std::shared_ptr<const SavedGame> Capture(const std::string& slot);
    void WriteOnTasks(const std::string& slot, std::shared_ptr<const SavedGame> saved);
    void FinishWrite(bool written);
    void CommitRestore();
    void Restore(const std::string& slot);
    void BeginChange(const SavedGame& saved);
    void FailWrite(std::string reason);
    void FailRestore(std::string reason);

    Engine& Host;
    std::optional<SaveSlotFiles> Files;
    std::vector<SavedGameSectionDefinition> Sections;
    std::map<std::string, std::shared_ptr<const SavedGame>, std::less<>> Remembered;
    std::vector<SavedGameSection> Unclaimed;
    std::optional<std::string> WriteWaiting;
    std::optional<std::string> RestoreWaiting;
    std::optional<std::vector<SavedGameSection>> SectionsWaiting;
    SaveStatus Writing = SaveStatus::Idle;
    SaveStatus Restoring = SaveStatus::Idle;
    std::uint64_t RestoreSerial = 0;
    bool FromBackup = false;
    std::string Reason;
    std::optional<InFlightWrite> InFlight;
    AsyncTaskHandle ReadTask;
};
