#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where an effect runs relative to the output transform. Scene effects see linear
// scene-referred light; display effects see what the transform produced.
enum class PostEffectDomain : std::uint8_t
{
    Scene,
    Display,
};

struct PostEffectId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(PostEffectId, PostEffectId) = default;
};

// A registered effect: a fragment shader against engine/shaders/post_effect.glsli
// and where it runs. Neutral data, so a module registers one with no rendering code.
struct PostEffectDesc
{
    std::string_view Name;
    std::span<const std::uint32_t> FragmentSpirv;
    PostEffectDomain Domain = PostEffectDomain::Display;
    std::int16_t Order = 0;
};

struct PostEffectType
{
    std::string Name;
    std::vector<std::uint32_t> FragmentSpirv;
    PostEffectDomain Domain = PostEffectDomain::Display;
    std::int16_t Order = 0;
};

// Every effect type this process can run. Owned by Engine; a game adds to it from
// Game::OnRegisterPostEffects at startup. Docs: docs/renderer/post-effects.md.
class PostEffectCatalog
{
public:
    // Null when the name or the (domain, order) is already taken, or the shader is
    // empty: effects compose in that order, so a duplicate would have none.
    [[nodiscard]] PostEffectId Register(const PostEffectDesc& desc);

    [[nodiscard]] PostEffectId Find(std::string_view name) const;
    [[nodiscard]] const PostEffectType* Get(PostEffectId id) const;
    [[nodiscard]] std::size_t Count() const { return Types.size(); }

private:
    std::vector<PostEffectType> Types;
};
