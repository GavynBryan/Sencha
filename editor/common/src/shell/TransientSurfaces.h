#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// Which interface is open on top of the content, in the order Escape takes it
// away: the highest priority first, the newest among equals, one at a time.
// Whoever opened one is told when it closes, however it closed.
class TransientSurfaces
{
public:
    using Id = std::uint32_t;

    [[nodiscard]] Id Open(int priority = 0);
    void Close(Id id);
    [[nodiscard]] bool IsOpen(Id id) const;
    [[nodiscard]] bool AnyOpen() const { return !Entries.empty(); }

    // Closes the one Escape reaches. False when none, and nothing changed.
    bool Escape();

    void SetClosedObserver(std::function<void(Id)> observer) { ClosedObserver = std::move(observer); }

private:
    struct Entry
    {
        Id Key = 0;
        int Priority = 0;
        std::uint64_t Order = 0;
    };

    std::vector<Entry> Entries;
    Id NextId = 1;
    std::uint64_t NextOrder = 1;
    std::function<void(Id)> ClosedObserver;
};
