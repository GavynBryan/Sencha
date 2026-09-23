#include <authored/WorldVocabulary.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <span>

VerbRegistry& InstallVerbRegistry(World& world)
{
    if (VerbRegistry* existing = world.TryGetResource<VerbRegistry>())
        return *existing;
    return world.AddResource<VerbRegistry>();
}

void InstallAuthoredVocabulary(World& world)
{
    (void)InstallVerbRegistry(world);
    if (world.TryGetResource<AuthoredQueryRegistry>() == nullptr)
        world.AddResource<AuthoredQueryRegistry>();
    if (world.TryGetResource<AuthoredEventRegistry>() == nullptr)
        world.AddResource<AuthoredEventRegistry>();
}

AuthoredQueryRegistry* FindAuthoredQueryRegistry(World& world)
{
    return world.TryGetResource<AuthoredQueryRegistry>();
}

const AuthoredQueryRegistry* FindAuthoredQueryRegistry(const World& world)
{
    return world.TryGetResource<AuthoredQueryRegistry>();
}

AuthoredEventRegistry* FindAuthoredEventRegistry(World& world)
{
    return world.TryGetResource<AuthoredEventRegistry>();
}

const AuthoredEventRegistry* FindAuthoredEventRegistry(const World& world)
{
    return world.TryGetResource<AuthoredEventRegistry>();
}

std::vector<std::string> AuthoredInstallationErrors(const World& world)
{
    std::vector<std::string> errors;
    const auto collect = [&errors](std::span<const std::string> from) {
        errors.insert(errors.end(), from.begin(), from.end());
    };
    if (const VerbRegistry* verbs = FindVerbRegistry(world))
        collect(verbs->InstallationErrors());
    if (const AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world))
        collect(queries->InstallationErrors());
    if (const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world))
        collect(events->InstallationErrors());
    return errors;
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
