#include <net/NetSpawnRecipe.h>

#include <cassert>
#include <utility>

void NetSpawnRecipes::Register(NetSpawnRecipeId id, Builder build)
{
    assert(id != kNetNoSpawnRecipe
           && "zero is the absence of a recipe and cannot name one");
    if (!build)
        return;
    Builders[id] = std::move(build);
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
