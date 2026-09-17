#pragma once

#include <core/logging/Logger.h>
#include <ui/UiDiagnostic.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

//=============================================================================
// UiDiagnosticLog
//
// The one owner of "the UI layer reported something". Every report -- the
// runtime's own refusals, the document engine's log, the cooker's notes --
// goes through Push, which records it for whoever drains and forwards it to
// the ordinary logger, so a host with no diagnostics consumer loses nothing
// and a host with one never has to scrape the log.
//
// Attribution is ambient: the runtime brackets a load, a rebuild, a restyle or
// a surface's update with a Scope, and an entry pushed inside it that names no
// screen or surface of its own takes the scope's. Scopes nest and restore.
//
// Bounded. A document with three hundred broken bindings is a document with
// one problem, and the ring keeping the newest is what lets the consumer see
// the end of the story; the sequence gap says the middle went.
//=============================================================================
class UiDiagnosticLog
{
public:
    struct Attribution
    {
        UiScreenHandle Screen;
        UiSurfaceId Surface;
    };

    class Scope
    {
    public:
        Scope(UiDiagnosticLog& log, Attribution attribution);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        // A screen learns its handle part-way through opening; the scope that
        // started with only the surface picks it up here.
        void Set(Attribution attribution);

    private:
        UiDiagnosticLog& Log;
        Attribution Previous;
    };

    explicit UiDiagnosticLog(Logger& log, std::size_t capacity = 256);

    [[nodiscard]] const Attribution& Current() const { return Active; }

    // Stamps the sequence and, where the entry left them blank, the current
    // attribution; records; forwards to the logger at the entry's severity.
    void Push(UiDiagnostic entry);

    // Everything since the last drain, oldest first. Destructive: one consumer.
    [[nodiscard]] std::vector<UiDiagnostic> Drain();

private:
    Logger& Sink;
    std::size_t Capacity;
    std::deque<UiDiagnostic> Ring;
    std::uint64_t NextSequence = 1;
    Attribution Active;
};
