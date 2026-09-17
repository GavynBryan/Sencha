#pragma once

#include <core/handle/ILifetimeOwner.h>
#include <core/handle/Owned.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// BackRouter
//
// Who gets "back out of this", and in what order.
//
// The shape is PlatformEventRouter's, which has held the same job for raw
// events: named consumers, offered in order, the first to claim it ends the
// offer. Two things differ, because this one carries dynamic UI rather than a
// fixed set wired at startup.
//
// Lifetime is a lease. A screen registering a lambda that captures `this` and
// then being destroyed -- by a load, an error, a scope exit -- would otherwise
// leave a dangling call behind it, so dropping the lease unregisters. That is
// the same answer InputContextSet gives to the same question.
//
// Ordering is bands, then most-recent-first inside Surface. Registration order
// alone is wrong for nested UI: an inventory registered at startup that later
// spawns a modal must not out-rank the modal it spawned. The outer bands are
// single structural positions and mirror the z-order the engine already
// documents -- diagnostics above authored UI above the application.
//
// This is the whole of the navigation machinery. There is no route table, no
// history and no screen registry: a page stack that needs one owns it.
//=============================================================================

enum class BackPriority : std::uint8_t
{
    // The debug console. First because raw-event consumption cannot suppress a
    // mapped action -- the router folds every event into the snapshot before
    // offering it to anyone -- so the console claiming Escape at the event
    // layer does not stop the action firing. It has to claim it here too.
    Diagnostics = 0,
    // An edit in progress. Its own band rather than the top of Surface because
    // an engine consumer registered at startup is older than every game one and
    // would lose the most-recent-first rule.
    TextEntry,
    // The application shell's page stack, while it is open. Above game UI: once
    // a menu is up it is what Back is for, and an inventory left open behind it
    // must not take the player's Resume press and close itself instead.
    Shell,
    // Game UI layers. Most recently registered first.
    Surface,
    // Opening the shell from gameplay. Last, so "Back opens the menu" is the
    // absence of anyone else wanting it rather than a claim over them.
    Fallback,
};

struct BackConsumerToken
{
    // Slot index plus one, so a default-constructed token stays invalid.
    std::uint32_t Value = 0;

    friend bool operator==(BackConsumerToken, BackConsumerToken) = default;
};

using BackConsumerLease = Owned<BackConsumerToken>;

class BackRouter final : public ILifetimeOwner
{
public:
    // True when this consumer handled it, which ends the offer.
    using Consumer = std::function<bool()>;

    [[nodiscard]] BackConsumerLease AddConsumer(std::string_view name,
                                                BackPriority priority,
                                                Consumer consumer);

    // Offers Back to each live consumer in order. True when one took it.
    [[nodiscard]] bool Dispatch();

    [[nodiscard]] std::size_t ConsumerCount() const;

    void Attach(std::uint64_t token) override;
    void Detach(std::uint64_t token) override;

private:
    struct Entry
    {
        std::string Name;
        BackPriority Priority = BackPriority::Surface;
        Consumer Handle;
        // Registration order, so the most recent wins inside a band without
        // depending on where the entry happens to sit in the vector.
        std::uint64_t Sequence = 0;
        bool Live = false;
    };

    std::vector<Entry> Entries;
    std::uint64_t NextSequence = 1;
};
