#include <core/console/ConsoleHistory.h>

ConsoleHistory::ConsoleHistory(std::size_t capacity)
    : Cap(capacity == 0 ? DefaultCapacity : capacity)
{
}

void ConsoleHistory::Push(std::string_view line)
{
    Cursor = 0;
    if (line.empty())
        return;
    if (!Entries.empty() && Entries.back() == line)
        return;

    Entries.emplace_back(line);
    while (Entries.size() > Cap)
        Entries.pop_front();
}

std::optional<std::string_view> ConsoleHistory::Prev()
{
    if (Entries.empty())
        return std::nullopt;
    if (Cursor < Entries.size())
        ++Cursor;
    return EntryAtCursor();
}

std::optional<std::string_view> ConsoleHistory::Next()
{
    if (Cursor <= 1)
    {
        Cursor = 0;
        return std::nullopt;
    }
    --Cursor;
    return EntryAtCursor();
}
