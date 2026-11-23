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
    // The game's own fields, carried as read; the engine reads none of it.
    JsonValue Game;
};

[[nodiscard]] bool operator==(const SavedGameSummary& a, const SavedGameSummary& b);

// A chunk carried as read and written back where it was: before HEAD or after the sections.
struct SavedGameChunk
{
    std::uint32_t Id = 0;
    std::uint32_t Version = 1;
    std::vector<std::byte> Payload;
    bool InPrefix = false;

    friend bool operator==(const SavedGameChunk&, const SavedGameChunk&) = default;
};

// A picture of a save for a menu, as the game gives it; the engine reads none of it.
struct SavedGameThumbnail
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // What the bytes are, as the game names it: "png", "rgba8".
    std::string Encoding;
    std::vector<std::byte> Bytes;

    friend bool operator==(const SavedGameThumbnail&, const SavedGameThumbnail&) = default;
};

// How the world differs from its authored content: docs/gameplay/saves.md.
struct SavedGame
{
    SavedGameSummary Summary;
    // THMB, as EncodeSavedGameThumbnail makes it; one of a version this build does not read is carried.
    std::optional<SavedGameChunk> Thumbnail;
    std::vector<DurableLevelState> Levels;
    std::vector<SavedGameSection> Sections;
    // Chunks this build does not know.
    std::vector<SavedGameChunk> Carried;
};

inline constexpr std::uint32_t kSavedGameFormatVersion = 2;

struct DecodedSavedGame
{
    std::optional<SavedGame> Saved;
    // Why Saved is empty.
    std::string Error;
    // The file's format, a chunk or a list this build reads is newer than this build's.
    bool Newer = false;
};

struct DecodedSavedGameSummary
{
    std::optional<SavedGameSummary> Summary;
    std::optional<SavedGameChunk> Thumbnail;
    std::string Error;
    bool Newer = false;
};

// Empty when it cannot be written.
[[nodiscard]] std::vector<std::byte> EncodeSavedGame(const SavedGame& saved);
[[nodiscard]] DecodedSavedGame DecodeSavedGame(std::span<const std::byte> bytes);
// Reads only the file's prefix up to HEAD, and checks it against HEAD.
[[nodiscard]] DecodedSavedGameSummary DecodeSavedGameSummary(std::span<const std::byte> prefix);
// Bytes from a file's start through HEAD, given those read so far; no more than were read once none can follow.
[[nodiscard]] std::size_t SavedGamePrefixWanted(std::span<const std::byte> read);

[[nodiscard]] SavedGameChunk EncodeSavedGameThumbnail(const SavedGameThumbnail& thumbnail);
// Empty for a chunk of a version this build does not read, or one that does not read.
[[nodiscard]] std::optional<SavedGameThumbnail> DecodeSavedGameThumbnail(const SavedGameChunk& chunk);
