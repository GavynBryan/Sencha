#pragma once

#include <cstdint>
#include <string>

// What a workspace tells the shell about itself, as plain facts. Each is a
// small read model with no presentation in it: a shell decides whether the
// document is a dial, a tab or a title, and a transport is keys or a menu.
// The operations beside them are actions in the ActionCatalog.

// The document the workspace is editing right now.
struct WorkspaceDocumentFacet
{
    std::string Label;
    bool Changed = false;
};

// Whether the workspace cooks and plays, and what that is doing now. The keys
// are the actions transport.cook, transport.play and transport.stop, which a
// workspace with a transport claims.
enum class TransportState : std::uint8_t
{
    None,
    Idle,
    Cooking,
    Playing,
    Failed,
};

struct WorkspaceTransportFacet
{
    TransportState State = TransportState::None;
    // A short report of what is happening: the cook step, the last failure.
    std::string Readout = {};
};
