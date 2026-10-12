#pragma once

#include "input/KeymapFile.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ActionCatalog;

// What claiming an action means: how to perform it, and whether it can be
// performed or is switched on right now. Empty predicates read as enabled and
// as not a toggle.
struct ActionHandler
{
    std::function<void()> Invoke;
    std::function<bool()> Enabled = {};
    std::function<bool()> Checked = {};
};

// The handlers one owner provides -- the application, a window, a workspace, a
// document, the tool in hand, a focused surface -- and that live exactly as
// long as it does.
class ActionContext
{
public:
    void Claim(std::string id, ActionHandler handler);
    void Release(std::string_view id);
    [[nodiscard]] const ActionHandler* Find(std::string_view id) const;

private:
    std::unordered_map<std::string, ActionHandler> Handlers;
};

// How close to the person's attention a context is. Resolution asks the
// closest first: a focused explorer before the tool in hand before the
// workspace before the window before the application.
enum class ActionRank : std::uint8_t
{
    Application,
    Window,
    Workspace,
    Document,
    Mode,
    Surface,
};

//=============================================================================
// ActionContextStack
//
// One window's answer to "what does this action do here": the contexts in play,
// asked closest first, and among contexts of one rank the most recently pushed.
// A context is in play while its Layer is alive, so a workspace closing or a
// surface losing focus takes its handlers with it and nothing can dangle.
//=============================================================================
class ActionContextStack
{
public:
    class Layer
    {
    public:
        Layer() = default;
        ~Layer() { Reset(); }
        Layer(Layer&& other) noexcept;
        Layer& operator=(Layer&& other) noexcept;
        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;

        void Reset();
        [[nodiscard]] bool IsActive() const { return Stack != nullptr; }

    private:
        friend class ActionContextStack;
        Layer(ActionContextStack& stack, std::uint64_t id) : Stack(&stack), Id(id) {}

        ActionContextStack* Stack = nullptr;
        std::uint64_t Id = 0;
    };

    ActionContextStack() = default;
    ~ActionContextStack();
    ActionContextStack(const ActionContextStack&) = delete;
    ActionContextStack& operator=(const ActionContextStack&) = delete;

    [[nodiscard]] Layer Push(const ActionContext& context, ActionRank rank);

    [[nodiscard]] const ActionHandler* Resolve(std::string_view id) const;

    struct State
    {
        bool Claimed = false;
        bool Enabled = false;
        bool Checked = false;
    };
    [[nodiscard]] State Query(std::string_view id) const;

    // Performs the action if something claims it and it is enabled. True when
    // something claims it, enabled or not: a chord for an action that exists
    // here but cannot run right now is still this window's, not the world's.
    bool Invoke(std::string_view id) const;
    // A chord is performed by the nearest claim that can act now: one that
    // cannot passes it outward, as a focused field passes a key it has no use for.
    bool Dispatch(const ActionCatalog& catalog, const KeyChord& chord) const;

private:
    struct Entry
    {
        std::uint64_t Id = 0;
        const ActionContext* Context = nullptr;
        ActionRank Rank = ActionRank::Application;
    };
    // Who answers an action, and with what; both null when nothing claims it.
    struct Claim
    {
        const Entry* By = nullptr;
        const ActionHandler* Handler = nullptr;
    };

    // The highest rank answers, and of equal rank the newest.
    [[nodiscard]] static bool Outranks(const Entry& entry, const Claim& other);
    [[nodiscard]] Claim Find(std::string_view id) const;
    [[nodiscard]] Claim FindEnabled(std::string_view id) const;
    static bool Perform(const Claim& claim);
    void Remove(std::uint64_t id);

    std::vector<Entry> Entries;
    std::uint64_t NextId = 1;
};
