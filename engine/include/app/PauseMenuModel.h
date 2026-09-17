#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class PauseMenu;

//=============================================================================
// PauseMenuModel
//
// What the shell's menu offers, and what each entry does.
//
// A data table rather than a branch pile: entries are a vector and commands are
// a lookup, so adding one costs an Add call and no engine edit, and the growth
// axis is the vector rather than an `if` chain that gets longer per feature.
//
// Authoring is by stable id. Positions are presentation output -- the document
// repeats over the published labels and reports which row was activated, and
// that index is resolved back to a command before anything acts on it -- so a
// game that reorders the menu does not silently change what a row does.
//=============================================================================

using PauseCommandId = StrongId<struct PauseCommandTag, std::uint32_t>;

// The three the stock menu ships with. Constants rather than names resolved at
// runtime: a game customising the menu addresses them directly.
inline constexpr PauseCommandId kPauseResume{ 1 };
inline constexpr PauseCommandId kPauseOptions{ 2 };
inline constexpr PauseCommandId kPauseExit{ 3 };

// What a command is handed when it runs: enough to drive the shell, and
// nothing that would let it reach past it.
struct PauseMenuContext
{
    PauseMenu& Menu;
};

using PauseCommandHandler = std::function<void(PauseMenuContext&)>;

struct PauseMenuEntry
{
    std::string Label;
    PauseCommandId Command;
    // Presented, but refusing to run. A save entry with nothing to save.
    bool Enabled = true;
};

class PauseMenuModel
{
public:
    // The title the document shows above the entries.
    void SetTitle(std::string title) { Title_ = std::move(title); }
    [[nodiscard]] const std::string& Title() const { return Title_; }

    // Which document the root page opens. A game replacing the presentation
    // names its own package here; leaving it alone takes the engine's, which is
    // mounted as a fallback root and so is shadowed by a game shipping its own
    // ui/pause.rml at the same virtual path.
    void SetRootPage(std::string package) { RootPage_ = std::move(package); }
    [[nodiscard]] const std::string& RootPage() const { return RootPage_; }

    // The options page, and the entry that reaches it.
    //
    // Options is not a special concept: it is a second page, and the stack that
    // Back already walks is the whole of the mechanism. The entry appears only
    // once a page exists, so it is never a button that does nothing.
    void SetOptionsPage(std::string package);
    [[nodiscard]] const std::string& OptionsPage() const { return OptionsPage_; }

    // -- entries -------------------------------------------------------------

    // Appends, and returns the id to address it by afterwards.
    PauseCommandId Add(std::string label, PauseCommandHandler handler);

    // Replaces what an existing command does, keeping its place and label. How
    // a game puts a confirmation in front of Quit, or a save behind it.
    bool SetHandler(PauseCommandId command, PauseCommandHandler handler);

    bool SetLabel(PauseCommandId command, std::string label);
    bool SetEnabled(PauseCommandId command, bool enabled);
    bool Remove(PauseCommandId command);

    // Moves `command` to sit directly before `before`. By id, because a menu
    // reordered by index is one where every later edit has to know what moved.
    bool MoveBefore(PauseCommandId command, PauseCommandId before);

    [[nodiscard]] const std::vector<PauseMenuEntry>& Entries() const { return Entries_; }

    // The labels, in order, as the document repeats over them.
    [[nodiscard]] std::vector<std::string> Labels() const;

    // Which command the document meant by the row it reported. Invalid when the
    // index names no row, which is what a stale click after a reorder looks
    // like.
    [[nodiscard]] PauseCommandId CommandAt(std::size_t index) const;

    [[nodiscard]] const PauseCommandHandler* Handler(PauseCommandId command) const;
    [[nodiscard]] bool IsEnabled(PauseCommandId command) const;

    // The stock menu: Resume, Options, and the host's own way out. `exitLabel`
    // is the host's because how you leave is a platform fact -- a desktop says
    // "Exit to Desktop", and a host that cannot terminate passes nothing and
    // gets no entry at all.
    void InstallDefaults(std::string_view exitLabel);

private:
    [[nodiscard]] std::size_t IndexOf(PauseCommandId command) const;

    std::string Title_ = "Paused";
    std::string RootPage_ = "asset://ui/pause.rml";
    std::string OptionsPage_;
    std::vector<PauseMenuEntry> Entries_;
    std::vector<std::pair<PauseCommandId, PauseCommandHandler>> Handlers_;
    std::uint32_t NextCommand = 4;   // past the three constants above
};
