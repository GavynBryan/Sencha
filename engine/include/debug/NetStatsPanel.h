#pragma once

#include <debug/IDebugPanel.h>

#include <memory>

class ConsoleRegistry;
class NetSession;
class NetStats;
class NetTickEstimator;
class PeerCommandRuntime;

//=============================================================================
// NetStatsPanel
//
// Debug-overlay window over a live session: role and identity, traffic rates by
// payload kind, and one row per peer with the round trip, the strike count, and
// how deep that peer's input is buffered.
//
// Buffer depth earns its place next to the round trip because the two are the
// same measurement from opposite ends. A peer whose ping is fine and whose
// queue is deep is being made late by this machine, not by the network, and
// that distinction is invisible from either number alone.
//
// Holds the session slot rather than the session, because a session comes and
// goes underneath a panel that is registered once for the process. Draws a line
// saying so when there is none, which is the normal state for a single-player
// game and not a fault worth hiding.
//=============================================================================
class NetStatsPanel : public IDebugPanel
{
public:
    NetStatsPanel(const std::unique_ptr<NetSession>& session,
                  const NetStats& traffic,
                  const NetTickEstimator& clock,
                  PeerCommandRuntime& commands,
                  ConsoleRegistry& console);

    void Draw() override;

private:
    const std::unique_ptr<NetSession>& Session;
    const NetStats& Traffic;
    const NetTickEstimator& Clock;
    PeerCommandRuntime& Commands;
    ConsoleRegistry& Console;
};
