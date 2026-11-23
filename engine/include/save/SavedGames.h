#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/EnumSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <save/SaveSlotFiles.h>
#include <save/SaveSlotListing.h>
#include <save/SavedGame.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
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
    // Reads only: a write settles held records and the zone store before any section captures.
    std::function<JsonValue(const World&)> Capture;
    // On every restore, once the restoring level change has installed zone
    // state and before the level arrives: given a section of Version or older,
    // or null data when the save has none, so nothing of the previous game stays.
    std::function<void(World&, std::uint32_t version, const JsonValue& data)> Restore;
};

// What the game adds to every save's summary, asked for at capture.
struct SavedGameSummaryDefinition
{
    // The summary's `game` member.
    std::function<JsonValue(const World&)> Describe;
    std::function<std::optional<SavedGameThumbnail>(const World&)> Thumbnail;
};

// Written by SavedGames itself after every section has captured.
inline constexpr std::string_view kPersistentIdsSection = "sencha.persistent_ids";

enum class SaveAdmission : std::uint8_t
{
    Accepted,
    Busy,
    BadSlot,
    NothingLoaded,
    NotAuthority,
    InSession,
};

template<>
struct EnumSchema<SaveAdmission>
{
    static constexpr std::array Values = {
        EnumValue{ SaveAdmission::Accepted, "accepted", "Accepted", "accepted" },
        EnumValue{ SaveAdmission::Busy, "busy", "Busy", "another is under way" },
        EnumValue{ SaveAdmission::BadSlot, "bad_slot", "Bad slot", "a slot is letters, digits, '-' and '_'" },
        EnumValue{ SaveAdmission::NothingLoaded, "nothing_loaded", "Nothing loaded", "nothing is loaded" },
        EnumValue{ SaveAdmission::NotAuthority, "not_authority", "Not the authority",
                   "this machine is not the authority" },
        EnumValue{ SaveAdmission::InSession, "in_session", "In a session", "a network session is running" },
    };
};

enum class SaveStatus : std::uint8_t
{
    Idle,
    Pending,
    Working,
    Done,
    Failed,
};

enum class RestoreProgress : std::uint8_t
{
    Idle,
    // Accepted; carried out at the next zone-residency phase.
    Pending,
    // The slot is read and decoded on the async lane.
    Reading,
    // The restore's level change is under way; its sections are put back once
    // the old level has gone.
    Loading,
    // The level is resident and every participant has its body back.
    BodiesRestored,
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

    // Without it a game keeps destroyed sets only: docs/gameplay/saves.md, "Level entities".
    void KeepLevelState();

    void SetSummary(SavedGameSummaryDefinition definition);

    // Refused for a name already registered and for kPersistentIdsSection.
    // A section the restored save carried under this name is no longer kept.
    [[nodiscard]] bool RegisterSection(SavedGameSectionDefinition section);
    void UnregisterSection(std::string_view name);

    // `arrival` names where the saved body comes back; the body section writes it.
    [[nodiscard]] SaveAdmission RequestWrite(std::string slot = std::string(kDefaultSaveSlot),
                                             std::string arrival = {});
    [[nodiscard]] SaveAdmission RequestRestore(std::string slot = std::string(kDefaultSaveSlot));

    [[nodiscard]] SaveStatus WriteStatus() const { return Writing; }
    [[nodiscard]] const std::string& WriteFailure() const { return WriteReason; }
    [[nodiscard]] RestoreProgress RestoreStatus() const { return Restoring; }
    [[nodiscard]] const std::string& RestoreFailure() const { return RestoreReason; }
    // Counts accepted restore requests, so a caller can tell a restore it has
    // seen through from a later one.
    [[nodiscard]] std::uint64_t Restores() const { return RestoreCount; }
    // The latest restore read the slot's backup because its file did not read.
    [[nodiscard]] bool RestoredFromBackup() const { return FromBackup; }
    // Each reads slot files' prefixes on the async lane and answers on the owner thread.
    void RequestList(std::function<void(std::vector<SaveSlotSummary>)> done);
    void RequestThumbnail(std::string slot, std::function<void(std::optional<SavedGameThumbnail>)> done);
    // The file, its backup, and what this session remembers of the slot.
    [[nodiscard]] SaveAdmission RequestDelete(std::string slot);
    // A write of `to` with what a restore of `from` reads, reported by WriteStatus.
    [[nodiscard]] SaveAdmission RequestCopy(std::string from, std::string to);

    // What a restore would find, from the prefixes of the slot's files.
    [[nodiscard]] bool Holds(std::string_view slot) const;
    // This session wrote or restored the slot; a file from an earlier run alone is not remembered.
    [[nodiscard]] bool Remembers(std::string_view slot) const { return Touched.contains(slot); }
    // The arrival of the write being captured; empty outside one.
    [[nodiscard]] const std::string& WritingArrival() const { return CapturingArrival; }

    void Commit();
    // After the level change commits: a restore's sections are put back.
    void RestoreSections();
    // After the level change advances: the restore follows it to its bodies.
    void Advance();
    // At shutdown, before the async lane stops: a write requested and not yet
    // on disk is written on this thread.
    void Finish();

private:
    struct InFlightWrite
    {
        std::string Slot;
        // The slot a copy reads; empty for a write.
        std::string From;
        // A write's save, released from memory once on disk.
        std::shared_ptr<const SavedGame> Saved;
        // Answers why it failed, or nothing.
        std::function<std::string()> Work;
        bool FromRemembered = false;
        AsyncTaskHandle Task;
    };

    [[nodiscard]] bool LevelSettling() const;
    // Why a settled level cannot be captured; nothing when it can.
    [[nodiscard]] std::optional<std::string> WriteRefusal() const;
    // Null when there is nothing to write to disk.
    [[nodiscard]] std::shared_ptr<const SavedGame> Capture(const std::string& slot, const std::string& arrival);
    [[nodiscard]] bool WritesTo(std::string_view slot) const;
    void StartWrite(InFlightWrite write);
    void FinishWrite(std::string failure);
    void CommitRestore();
    void Restore(const std::string& slot);
    void BeginChange(std::shared_ptr<const SavedGame> saved);
    void RestoreHeldLists(World& world, const SavedGame& saved);
    void FailWrite(std::string reason);
    void FailRestore(std::string reason);

    Engine& Host;
    std::optional<SaveSlotFiles> Files;
    std::vector<SavedGameSectionDefinition> Sections;
    SavedGameSummaryDefinition SummaryDefinition;
    std::set<std::string, std::less<>> Touched;
    // The one save not yet on disk, which a restore of its slot prefers.
    std::optional<std::string> UnwrittenSlot;
    std::shared_ptr<const SavedGame> Unwritten;
    std::vector<SavedGameSection> Unclaimed;
    std::vector<SavedGameChunk> UnclaimedChunks;
    std::optional<std::string> WriteWaiting;
    std::string ArrivalWaiting;
    std::string CapturingArrival;
    std::optional<std::string> RestoreWaiting;
    std::shared_ptr<const SavedGame> SectionsWaiting;
    SaveStatus Writing = SaveStatus::Idle;
    RestoreProgress Restoring = RestoreProgress::Idle;
    std::uint64_t RestoreCount = 0;
    std::uint64_t RestoreSerial = 0;
    std::uint64_t SeenChange = 0;
    bool FromBackup = false;
    std::string WriteReason;
    std::string RestoreReason;
    std::optional<InFlightWrite> InFlight;
    AsyncTaskHandle ReadTask;
};
