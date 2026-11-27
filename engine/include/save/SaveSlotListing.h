#pragma once

#include <save/SaveSlotFiles.h>
#include <save/SavedGame.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One slot as a load menu shows it, read from its file's prefix: docs/gameplay/saves.md, "Slots".
struct SaveSlotSummary
{
    std::string Slot;
    SavedGameSummary Summary;
    bool HasThumbnail = false;
    // Written by a newer build; this build restores neither it nor its backup in its place.
    bool Newer = false;
    // The slot's file did not read; Summary is its backup's, which a restore reads.
    bool FromBackup = false;
    // Why the slot's file did not read; with no backup offered, why the slot cannot be restored.
    std::string Error;

    [[nodiscard]] bool Readable() const { return Error.empty() || FromBackup; }
};

struct SaveSlotListing
{
    std::vector<SaveSlotSummary> Slots;
    std::uint64_t BytesRead = 0;
};

// Each reads a file only as far as its HEAD, never its levels.
[[nodiscard]] SaveSlotListing ListSaveSlots(const SaveSlotFiles& files);
[[nodiscard]] SaveSlotSummary ReadSaveSlotSummary(const SaveSlotFiles& files, std::string_view slot);
[[nodiscard]] std::optional<SavedGameThumbnail> ReadSaveSlotThumbnail(const SaveSlotFiles& files, std::string_view slot);
