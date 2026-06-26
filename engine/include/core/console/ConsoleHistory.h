#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

// Recall buffer for submitted console command lines, driven by Up/Down in the
// input box. Plain data: no ImGui, no registry dependency. The console view owns
// one of these and moves the cursor as the user navigates.
//
// The cursor counts steps back from the draft (in-progress) line: 0 is the
// draft, 1 is the newest entry, Size() is the oldest. Editing the buffer resets
// the cursor so the next Up starts from the newest entry again.
class ConsoleHistory
{
public:
    static constexpr std::size_t DefaultCapacity = 256;

    explicit ConsoleHistory(std::size_t capacity = DefaultCapacity);

    // Append a submitted line. Blank lines and a line identical to the most
    // recent entry are dropped (consecutive duplicates add nothing to recall).
    // Resets the cursor to the draft position.
    void Push(std::string_view line);

    // Step toward older entries (Up). Returns the entry now under the cursor, or
    // nullopt when the history is empty. At the oldest entry the cursor stays put
    // and keeps returning it.
    [[nodiscard]] std::optional<std::string_view> Prev();

    // Step toward newer entries (Down). Returns the entry now under the cursor,
    // or nullopt once the cursor reaches the draft position (the caller restores
    // the in-progress text / clears the buffer).
    [[nodiscard]] std::optional<std::string_view> Next();

    // Re-seat the cursor at the draft position. Call when the user edits the
    // buffer so navigation restarts from the newest entry.
    void ResetCursor() { Cursor = 0; }

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] bool Empty() const { return Entries.empty(); }
    [[nodiscard]] std::size_t Capacity() const { return Cap; }

private:
    // Cursor in [1, Size()] indexes from the newest end.
    [[nodiscard]] std::string_view EntryAtCursor() const
    {
        return Entries[Entries.size() - Cursor];
    }

    std::deque<std::string> Entries; // front = oldest, back = newest
    std::size_t Cap;
    std::size_t Cursor = 0;
};
