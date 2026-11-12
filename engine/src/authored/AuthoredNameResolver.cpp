#include <authored/AuthoredNameResolver.h>

#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>
#include <utility>

AuthoredNameResolver::AuthoredNameResolver(const World& world)
    : Tags(world.TryGetResource<GameplayTagRegistry>())
    , Effects(world.TryGetResource<EffectRegistry>())
{
}

void AuthoredNameResolver::Tag(std::string_view name, GameplayTagId& out)
{
    if (name.empty() || !Resolved())
        return;
    out = Tags != nullptr ? Tags->FindTag(name) : GameplayTagId{};
    if (!out.IsValid())
        Fail(std::format("no gameplay tag is declared as '{}'", name));
}

void AuthoredNameResolver::Effect(std::string_view name, EffectId& out)
{
    if (name.empty() || !Resolved())
        return;
    out = Effects != nullptr ? Effects->Find(name) : EffectId{};
    if (Effects == nullptr || Effects->Get(out) == nullptr)
        Fail(std::format("no effect is declared as '{}'", name));
}

void AuthoredNameResolver::Fail(std::string problem)
{
    if (Resolved())
        Problem_ = std::move(problem);
}
