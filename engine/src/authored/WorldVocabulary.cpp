#include <authored/WorldVocabulary.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

VerbRegistry& InstallVerbRegistry(World& world)
{
    if (VerbRegistry* existing = world.TryGetResource<VerbRegistry>())
        return *existing;
    return world.AddResource<VerbRegistry>();
}

VerbRegistry* FindVerbRegistry(World& world)
{
    return world.TryGetResource<VerbRegistry>();
}

const VerbRegistry* FindVerbRegistry(const World& world)
{
    return world.TryGetResource<VerbRegistry>();
}

VerbBindingEnvironment MakeVerbBindingEnvironment(const World& world)
{
    return VerbBindingEnvironment{
        .Verbs = world.TryGetResource<VerbRegistry>(),
        .Tags = world.TryGetResource<GameplayTagRegistry>(),
        .Assets = nullptr,
        .DataAssets = nullptr,
    };
}
