#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What takes an open transient away besides closing it directly.
//   Light:      a press anywhere outside it, or Escape.
//   Explicit:   Escape, or an action that closes it; presses elsewhere do not.
//   OwnerBound: only its owner closing it, or its parent closing.
enum class TransientDismiss : std::uint8_t
{
    Light,
    Explicit,
    OwnerBound,
};

struct TransientSpec
{
    std::string Owner;
    std::uint32_t Parent = 0;
    TransientDismiss Dismiss = TransientDismiss::Light;
    // Escape asks the highest-priority open transient first, and the newest
    // among equals.
    int Priority = 0;
};

//=============================================================================
// TransientSurfaces
//
// Which interface is open on top of the content, and what makes each go away.
// The mechanism only: "popover", "drawer" and "explorer" are presets of the
// spec above chosen by the shell's policy, so a nested explorer, two cards or
// a comparison view need nothing new here.
//
// Escape takes away one transient at a time and reports whether it did, so the
// caller falls through to the workspace's own escape steps (cancel the
// interaction, then deselect) only when nothing is left to close. A pinned
// transient stays through Escape and outside presses until unpinned.
//=============================================================================
class TransientSurfaces
{
public:
    using Id = std::uint32_t;

    [[nodiscard]] Id Open(TransientSpec spec);
    // Closes it and everything opened from it.
    void Close(Id id);
    void CloseOwnedBy(std::string_view owner);

    void SetPinned(Id id, bool pinned);
    [[nodiscard]] bool IsPinned(Id id) const;
    [[nodiscard]] bool IsOpen(Id id) const;
    [[nodiscard]] std::vector<Id> OpenIds() const;

    // Closes the one Escape reaches. False when none, and nothing changed.
    bool Escape();
    // A press landed outside every transient in `inside` (the ones it hit and
    // their ancestors are spared): the light ones go.
    void PressedOutside(const std::vector<Id>& inside);

    // Told of each transient as it closes, however it closed.
    void SetClosedObserver(std::function<void(Id)> observer) { ClosedObserver = std::move(observer); }

private:
    struct Entry
    {
        Id Key = 0;
        TransientSpec Spec;
        bool Pinned = false;
        std::uint64_t Order = 0;
    };

    [[nodiscard]] const Entry* Find(Id id) const;
    [[nodiscard]] bool IsAncestorOf(Id ancestor, Id id) const;

    std::vector<Entry> Entries;
    Id NextId = 1;
    std::uint64_t NextOrder = 1;
    std::function<void(Id)> ClosedObserver;
};
