#pragma once

#include <core/console/ConsoleTypes.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class ConsoleRegistry;

//=============================================================================
// CVarArchive
//
// Where a player's settings live between runs: the cvars carrying
// CVarFlags::Archive, written to one file and applied at startup.
//
// The file is a record of assignments, not a script. It is the same JSON shape
// engine.json's "cvars" section already uses, read through JsonParse and
// applied through the registry's ordinary set path, so the type conversion,
// phase rules, permission rules and diagnostics are the console's rather than
// a second set written here. A name the file contains that is not a cvar is
// queued as an unresolved assignment; there is no path by which an entry
// becomes a console *command*, which matters for a file sitting in a
// user-writable directory.
//
// Values are written as native JSON scalars rather than as text. std::to_chars
// gives a double its shortest round-tripping form, so a setting reloads as the
// number that was saved instead of one rounded through a printf default.
//
// This type names no platform: the file path is chosen by whoever owns the
// platform layer and handed in, which is also what lets a test point one at a
// temporary directory.
//=============================================================================
class CVarArchive
{
public:
    explicit CVarArchive(std::filesystem::path file);

    // The file an application's settings live in under `root`:
    // <root>/<slug of appName>/settings.json, the slug folding anything that
    // is not alphanumeric into single separators. The application's name is
    // therefore its settings namespace -- renaming a game moves its settings,
    // and two names that slug alike ("My Game", "my-game") share one file.
    // That is a consequence of naming by display name, not a uniqueness
    // guarantee, and it is the caller's to know.
    [[nodiscard]] static std::filesystem::path FileFor(const std::filesystem::path& root,
                                                       std::string_view appName);

    // Applies what was saved. A missing file is the ordinary first-run case and
    // is not an error.
    //
    // Call after the engine's own cvars register and BEFORE the startup script,
    // so precedence reads command line, then saved settings, then defaults. A
    // name no cvar claims yet is queued rather than dropped, which is what lets
    // a game module's setting survive a run where the module registered late.
    void Load(ConsoleRegistry& registry, ConsolePhase phase);

    // Writes every Archive cvar whose value differs from its default.
    //
    // Call at explicit commit boundaries -- closing a settings screen, shutting
    // down -- not every frame. A slider dragged for three seconds should cost
    // one write, which is why this is not driven from a frame phase.
    //
    // Returns whether anything was written. Nothing to save costs a counter
    // comparison and touches no disk.
    bool Save(const ConsoleRegistry& registry);

    // Whether a committed write has changed an Archive cvar since the last
    // Save or Load. Reads one counter; it does not walk the registry.
    [[nodiscard]] bool IsDirty(const ConsoleRegistry& registry) const;

    [[nodiscard]] const std::filesystem::path& File() const { return Path; }

    // One entry per malformed thing the last Load found, with enough detail to
    // fix it by hand. A bad entry costs itself and not the rest of the file.
    [[nodiscard]] const std::vector<std::string>& LoadDiagnostics() const
    {
        return Diagnostics;
    }

private:
    std::filesystem::path Path;
    std::vector<std::string> Diagnostics;

    // The registry's archive revision as of the last Load or Save, which is what
    // makes "has anything changed" a comparison rather than a scan.
    std::uint64_t SyncedRevision = 0;
};
