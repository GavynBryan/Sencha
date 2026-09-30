#pragma once

#include "documents/DocumentRef.h"
#include "observers/WeakObserverList.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class ProblemSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

// Something wrong, where it is, and what reported it. A problem names the
// document it is in rather than carrying a way to reach it: showing one is the
// host's to do, through the workspace that owns the document.
struct Problem
{
    ProblemSeverity Severity = ProblemSeverity::Warning;
    std::string Message;
    // Where inside the document, in the reporter's own terms: a field path, a
    // line, an element.
    std::string Location = {};
    std::optional<DocumentRef> Document = {};
};

//=============================================================================
// ProblemList
//
// Every located problem the editor knows about right now, gathered from the
// things that find them -- the cook, the UI layer's diagnostics, a save that
// could not complete, a data document's validation. Each source republishes its
// whole set whenever it changes, so a problem that was fixed is gone rather
// than left for someone to dismiss.
//=============================================================================
class ProblemList
{
public:
    using ObserverFn = WeakObserverList<>::Fn;

    void Publish(std::string_view source, std::vector<Problem> problems);
    void Clear(std::string_view source) { Publish(source, {}); }

    [[nodiscard]] std::vector<const Problem*> All() const;
    [[nodiscard]] std::size_t Count(ProblemSeverity atLeast) const;

    [[nodiscard]] std::shared_ptr<ObserverFn> Subscribe(ObserverFn fn) { return Observers.Subscribe(std::move(fn)); }

private:
    // Sources in the order they first reported, so the list reads the same way
    // from one frame to the next.
    std::vector<std::string> Order;
    std::unordered_map<std::string, std::vector<Problem>> BySource;
    WeakObserverList<> Observers;
};
