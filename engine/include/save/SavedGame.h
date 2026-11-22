#pragma once

#include <core/json/JsonValue.h>
#include <world/LevelReference.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateStore.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Named data a save carries beside the world's own: what the engine and a game
// register (SavedGameSections), and anything a newer build wrote, kept as read.
struct SavedGameSection
{
    std::string Name;
    std::uint32_t Version = 1;
    JsonValue Data;
};

struct SavedGameSummary
{
    LevelReference Level;
    std::optional<ZoneId> Focus;
    std::uint64_t SavedAtUnixSeconds = 0;

    friend bool operator==(const SavedGameSummary&, const SavedGameSummary&) = default;
};

// How the world differs from its authored content: docs/gameplay/saves.md.
struct SavedGame
{
    SavedGameSummary Summary;
    std::vector<DurableZoneState> Zones;
    std::vector<SavedGameSection> Sections;
};

inline constexpr std::uint32_t kSavedGameFormatVersion = 1;

struct DecodedSavedGame
{
    std::optional<SavedGame> Saved;
    // Why Saved is empty.
    std::string Error;
    // The file's format, or a chunk this build reads, is newer than this build's.
    bool Newer = false;
};

// Empty when it cannot be written.
[[nodiscard]] std::vector<std::byte> EncodeSavedGame(const SavedGame& saved);
[[nodiscard]] DecodedSavedGame DecodeSavedGame(std::span<const std::byte> bytes);
