#pragma once

#include <ecs/EntityId.h>
#include <presentation/PresentationOwner.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// Cosmetic contributions per key, one per owner, for the mechanisms several writers
// feed at once. `Contribution` carries `PresentationOwner Owner` and an ordered
// `Order`. The store never reads simulation state and no fixed-tick system reads it.
// Docs: docs/renderer/presentation-contributions.md.
template <typename Key, typename Contribution, typename Hash = std::hash<Key>>
class PresentationContributions
{
public:
    // Adds or replaces the contribution of `contribution.Owner`, returning what it
    // replaced.
    std::optional<Contribution> Set(const Key& key, Contribution contribution)
    {
        std::vector<Contribution>& held = ByKey[key];
        const auto existing = std::ranges::find_if(
            held, [&](const Contribution& other) { return other.Owner == contribution.Owner; });
        if (existing == held.end())
        {
            held.push_back(std::move(contribution));
            return std::nullopt;
        }
        return std::exchange(*existing, std::move(contribution));
    }

    std::optional<Contribution> Clear(const Key& key, const PresentationOwner& owner)
    {
        const auto found = ByKey.find(key);
        if (found == ByKey.end())
            return std::nullopt;
        std::vector<Contribution>& held = found->second;
        const auto existing = std::ranges::find_if(
            held, [&](const Contribution& other) { return other.Owner == owner; });
        if (existing == held.end())
            return std::nullopt;
        std::optional<Contribution> cleared = std::move(*existing);
        held.erase(existing);
        if (held.empty())
            ByKey.erase(found);
        return cleared;
    }

    // Removes every contribution `shouldRemove(key, contribution)` names, and hands
    // each to `removed(key, contribution)` after the store no longer holds it.
    template <typename Predicate, typename Removed>
    void RemoveWhere(Predicate&& shouldRemove, Removed&& removed)
    {
        Pending.clear();
        for (auto entry = ByKey.begin(); entry != ByKey.end();)
        {
            std::vector<Contribution>& held = entry->second;
            for (auto it = held.begin(); it != held.end();)
            {
                if (!shouldRemove(entry->first, *it))
                {
                    ++it;
                    continue;
                }
                Pending.emplace_back(entry->first, std::move(*it));
                it = held.erase(it);
            }
            entry = held.empty() ? ByKey.erase(entry) : std::next(entry);
        }
        for (std::pair<Key, Contribution>& gone : Pending)
            removed(gone.first, gone.second);
        Pending.clear();
    }

    template <typename Removed>
    void ClearWriter(PresentationWriterId writer, Removed&& removed)
    {
        RemoveWhere([writer](const Key&, const Contribution& contribution) { return contribution.Owner.Writer == writer; },
                    std::forward<Removed>(removed));
    }

    // Removes whatever a holder entity held once `isAlive` says it has gone, among the
    // contributions `applies` accepts: a kind of contribution that outlives its holder
    // by design says so here.
    template <typename IsAlive, typename Removed, typename Applies>
    void SweepDeadHolders(IsAlive&& isAlive, Removed&& removed, Applies&& applies)
    {
        RemoveWhere(
            [&isAlive, &applies](const Key&, const Contribution& contribution) {
                const LeaseOwner& holder = contribution.Owner.Holder;
                return applies(contribution) && holder.IsSet() && !isAlive(holder.Entity);
            },
            std::forward<Removed>(removed));
    }

    template <typename IsAlive, typename Removed>
    void SweepDeadHolders(IsAlive&& isAlive, Removed&& removed)
    {
        SweepDeadHolders(std::forward<IsAlive>(isAlive), std::forward<Removed>(removed),
                         [](const Contribution&) { return true; });
    }

    // Every contribution a key loses when the key itself goes.
    template <typename Removed>
    void ForgetKey(const Key& key, Removed&& removed)
    {
        const auto found = ByKey.find(key);
        if (found == ByKey.end())
            return;
        std::vector<Contribution> held = std::move(found->second);
        ByKey.erase(found);
        for (Contribution& contribution : held)
            removed(key, contribution);
    }

    [[nodiscard]] const std::vector<Contribution>* Find(const Key& key) const
    {
        const auto found = ByKey.find(key);
        return found != ByKey.end() ? &found->second : nullptr;
    }

    // The winner among the contributions `admits` accepts: the highest Order, then
    // the owner PresentationOwnerPrecedes puts first.
    template <typename Admits>
    [[nodiscard]] const Contribution* Resolve(const Key& key, Admits&& admits) const
    {
        const std::vector<Contribution>* held = Find(key);
        if (held == nullptr)
            return nullptr;
        const Contribution* winner = nullptr;
        for (const Contribution& contribution : *held)
        {
            if (!admits(contribution))
                continue;
            if (winner == nullptr || contribution.Order > winner->Order
                || (contribution.Order == winner->Order && PresentationOwnerPrecedes(contribution.Owner, winner->Owner)))
                winner = &contribution;
        }
        return winner;
    }

    [[nodiscard]] const Contribution* Resolve(const Key& key) const
    {
        return Resolve(key, [](const Contribution&) { return true; });
    }

    // In no meaningful order: a caller sorts what it derives from this.
    template <typename Visit>
    void ForEachKey(Visit&& visit) const
    {
        for (const auto& [key, held] : ByKey)
            visit(key, held);
    }

    template <typename Visit>
    void ForEachKeyMutable(Visit&& visit)
    {
        for (auto& [key, held] : ByKey)
            visit(key, held);
    }

    [[nodiscard]] std::size_t KeyCount() const { return ByKey.size(); }

private:
    std::unordered_map<Key, std::vector<Contribution>, Hash> ByKey;
    std::vector<std::pair<Key, Contribution>> Pending;
};
