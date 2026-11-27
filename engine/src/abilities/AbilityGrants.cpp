#include <abilities/AbilityGrants.h>

#include <ecs/World.h>

#include <algorithm>

namespace
{
    AbilityGrantLists& ListsOf(World& world)
    {
        return world.HasResource<AbilityGrantLists>() ? world.GetResource<AbilityGrantLists>()
                                                      : world.AddResource<AbilityGrantLists>();
    }

    AbilityGrantId Grant(std::vector<AbilityGrant>& entries, std::uint32_t& nextId, AbilityId ability,
                         AbilityGrantKind kind, std::uint16_t entry, EntityId holder, PersistableRef source)
    {
        if (!ability.IsValid() || entries.size() >= kAbilityGrantCapacity)
            return AbilityGrantId{};
        const AbilityGrantId id{ nextId++ };
        entries.push_back(AbilityGrant{ id, ability, kind, holder, source, entry });
        return id;
    }

    bool Revoke(std::vector<AbilityGrant>& entries, AbilityGrantId id)
    {
        return std::erase_if(entries, [id](const AbilityGrant& grant) { return grant.Id == id; }) > 0;
    }

    void Count(AbilityGrants& row, std::span<const AbilityGrant> entries)
    {
        const auto ofKind = [&](AbilityGrantKind kind) {
            return static_cast<std::uint16_t>(std::ranges::count(entries, kind, &AbilityGrant::Kind));
        };
        row.Count = static_cast<std::uint16_t>(entries.size());
        row.EffectHeld = ofKind(AbilityGrantKind::Effect);
        row.Transient = ofKind(AbilityGrantKind::Transient);
    }
}

void ComponentTraits<AbilityGrants>::OnAdd(AbilityGrants& grants, World& world, EntityId)
{
    if (AbilityGrantLists* lists = world.TryGetResource<AbilityGrantLists>())
        lists->Retain(grants.Entries);
}

void ComponentTraits<AbilityGrants>::OnRemove(const AbilityGrants& grants, World& world, EntityId)
{
    if (AbilityGrantLists* lists = world.TryGetResource<AbilityGrantLists>())
        lists->Release(grants.Entries);
}

std::span<const AbilityGrant> AbilityGrantsOf(const World& world, const AbilityGrants& grants)
{
    const AbilityGrantLists* lists = world.TryGetResource<AbilityGrantLists>();
    return lists != nullptr ? lists->Read(grants.Entries) : std::span<const AbilityGrant>{};
}

std::span<const AbilityGrant> AbilityGrantsOf(const World& world, EntityId actor)
{
    const AbilityGrants* grants =
        world.IsAlive(actor) && world.IsRegistered<AbilityGrants>() ? world.TryGet<AbilityGrants>(actor) : nullptr;
    return grants != nullptr ? AbilityGrantsOf(world, *grants) : std::span<const AbilityGrant>{};
}

const AbilityGrant* FindAbilityGrant(std::span<const AbilityGrant> grants, AbilityGrantId id)
{
    const auto found = std::ranges::find(grants, id, &AbilityGrant::Id);
    return found != grants.end() ? &*found : nullptr;
}

AbilityGrantId GrantAbility(AbilityGrantList& grants, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId holder, PersistableRef source)
{
    return Grant(grants.Entries, grants.NextId, ability, kind, entry, holder, source);
}

AbilityGrantId GrantAbility(World& world, EntityId actor, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId holder, PersistableRef source)
{
    const AbilityGrants* held = world.IsAlive(actor) ? std::as_const(world).TryGet<AbilityGrants>(actor) : nullptr;
    if (held == nullptr || !ability.IsValid() || held->Count >= kAbilityGrantCapacity)
        return AbilityGrantId{};
    AbilityGrants& row = *world.TryGet<AbilityGrants>(actor);
    std::vector<AbilityGrant>& entries = ListsOf(world).Edit(row.Entries);
    const AbilityGrantId id = Grant(entries, row.NextId, ability, kind, entry, holder, source);
    Count(row, entries);
    return id;
}

bool RevokeAbilityGrant(AbilityGrantList& grants, AbilityGrantId id)
{
    return Revoke(grants.Entries, id);
}

bool RevokeAbilityGrant(World& world, EntityId actor, AbilityGrantId id)
{
    if (FindAbilityGrant(AbilityGrantsOf(world, actor), id) == nullptr)
        return false;
    AbilityGrants& row = *world.TryGet<AbilityGrants>(actor);
    std::vector<AbilityGrant>& entries = ListsOf(world).Edit(row.Entries);
    (void)Revoke(entries, id);
    Count(row, entries);
    return true;
}

AbilityGrantList CopyAbilityGrants(const World& world, EntityId actor)
{
    AbilityGrantList copy;
    if (const AbilityGrants* grants = world.IsRegistered<AbilityGrants>() ? world.TryGet<AbilityGrants>(actor) : nullptr)
    {
        const std::span<const AbilityGrant> entries = AbilityGrantsOf(world, *grants);
        copy.Entries.assign(entries.begin(), entries.end());
        copy.NextId = grants->NextId;
    }
    return copy;
}

AbilityGrants MakeAbilityGrants(World& world, const AbilityGrantList& grants)
{
    AbilityGrants row;
    if (!grants.Entries.empty())
        row.Entries = ListsOf(world).Make(grants.Entries);
    Count(row, grants.Entries);
    row.NextId = grants.NextId;
    return row;
}

void SetAbilityGrants(World& world, EntityId actor, const AbilityGrantList& grants)
{
    const AbilityGrants row = MakeAbilityGrants(world, grants);
    if (world.HasComponent<AbilityGrants>(actor))
        (void)world.ReplaceComponent<AbilityGrants>(actor, row);
    else
        world.AddComponent<AbilityGrants>(actor, row);
}
