#pragma once

#include <cstdint>
#include <optional>

// The keys a document reports by number when it hands a key event to its host:
// data-event-keydown="menu_key(ev.key_identifier)". A text field that has focus
// takes every key, so a document is the only thing that sees them; this is the
// vocabulary its host reads them back in, without learning which document
// engine numbered them.
enum class UiKey : std::uint8_t
{
    Tab,
    Return,
    Escape,
    Up,
    Down,
};

// The key a document reported, or nullopt for one outside this vocabulary.
[[nodiscard]] std::optional<UiKey> UiKeyFromEvent(std::int64_t keyIdentifier);
