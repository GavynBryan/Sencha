#include <net/NetSpawnRecipe.h>

#include <cassert>
#include <utility>

bool NetSpawnRecipes::Register(NetSpawnRecipeId id, Builder build)
{
    // Refused rather than asserted, so a release build fails the same way a
    // debug one does. A registration that silently did nothing is the shape
    // that reaches a player: the id stays unknown, every entity naming it
    // arrives bare, and nothing along the way said so.
    if (id == kNetNoSpawnRecipe || !build)
        return false;
    if (Builders.find(id) != Builders.end())
        return false;

    Builders.emplace(id, std::move(build));
    return true;
}

bool NetSpawnRecipes::Build(NetSpawnRecipeId id, World& world, EntityId entity) const
{
    if (id == kNetNoSpawnRecipe)
        return false;

    const auto it = Builders.find(id);
    if (it == Builders.end())
        return false;

    it->second(world, entity);
    return true;
}

bool NetSpawnRecipes::Contains(NetSpawnRecipeId id) const
{
    return Builders.find(id) != Builders.end();
}
