#pragma once

#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <string>
#include <string_view>

class EffectRegistry;
class GameplayTagRegistry;
class World;

// Resolves the tag and effect names a component authors, keeping the first one
// that does not resolve. An empty name is left unset and is no problem.
class AuthoredNameResolver
{
public:
    explicit AuthoredNameResolver(const World& world);

    void Tag(std::string_view name, GameplayTagId& out);
    void Effect(std::string_view name, EffectId& out);
    // Records a problem of the caller's own; later names are then left alone.
    void Fail(std::string problem);

    [[nodiscard]] bool Resolved() const { return Problem_.empty(); }
    [[nodiscard]] const std::string& Problem() const { return Problem_; }

private:
    const GameplayTagRegistry* Tags = nullptr;
    const EffectRegistry* Effects = nullptr;
    std::string Problem_;
};
