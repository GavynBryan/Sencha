#pragma once

#include "observers/WeakObserverList.h"

#include <core/assets/AssetRef.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>

// A resource an operation consumes, as the operation declares it: the brush
// consumes a Material, placement a Scene. Several roles are allowed; a shell
// shows the primary one where "what I'm holding" belongs.
struct ResourceRole
{
    std::string_view Role;
    AssetType Type = AssetType::Unknown;
    bool Primary = false;
};

//=============================================================================
// ActiveResources
//
// What the person is holding, per kind of asset: the one the next operation
// will consume, and a few stashed beside it for one-click recall. View state,
// not document state: never saved with a level, never an undo step.
//=============================================================================
class ActiveResources
{
public:
    static constexpr std::size_t kStashSlots = 3;
    using StashSlots = std::array<AssetRef, kStashSlots>;
    using ObserverFn = WeakObserverList<AssetType>::Fn;

    // An empty ref until something of that kind is picked.
    [[nodiscard]] const AssetRef& Active(AssetType type) const;
    // The ref's own type decides which kind it becomes active for.
    void SetActive(const AssetRef& ref);

    [[nodiscard]] const StashSlots& Stash(AssetType type) const;
    // What clicking a stash slot means: an empty slot keeps the active one, a
    // filled one trades places with it.
    void UseStashSlot(AssetType type, std::size_t slot);
    void StoreInStash(AssetType type, std::size_t slot);
    void ClearStashSlot(AssetType type, std::size_t slot);

    // Told the kind whose active resource changed; subscribed while the token
    // is kept.
    [[nodiscard]] std::shared_ptr<ObserverFn> Subscribe(ObserverFn fn) { return Observers.Subscribe(std::move(fn)); }

private:
    struct Holding
    {
        AssetRef Active;
        StashSlots Stash{};
    };

    std::unordered_map<AssetType, Holding> Held;
    WeakObserverList<AssetType> Observers;
};
