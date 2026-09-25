#pragma once

#include <anim/AnimFacts.h>
#include <anim/AnimTypes.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Reads one entity's value. False when the entity does not have what the
// provider reads; the slot then keeps its previous value.
using AnimFactReadFn = bool (*)(const World& world, EntityId entity, const void* context,
                                std::uint32_t& out);

struct AnimFactProvider
{
    std::string Slot;
    AnimFactKind Kind = AnimFactKind::Float;
    AnimFactReadFn Read = nullptr;
    const void* Context = nullptr;
};

namespace AnimFactProviderDetail
{
    template <typename T>
    struct MemberTraits;

    template <typename C, typename F>
    struct MemberTraits<F C::*>
    {
        using Component = C;
        using Field = F;
    };

    template <typename F>
    constexpr AnimFactKind KindOf()
    {
        if constexpr (std::is_same_v<F, bool>)
            return AnimFactKind::Bool;
        else if constexpr (std::is_floating_point_v<F>)
            return AnimFactKind::Float;
        else if constexpr (std::is_same_v<F, GameplayTagId>)
            return AnimFactKind::Tag;
        else
        {
            static_assert(std::is_integral_v<F>, "a fact field is bool, a number, or a tag");
            return AnimFactKind::Int;
        }
    }

    template <typename F>
    std::uint32_t Encode(const F& value)
    {
        if constexpr (std::is_same_v<F, bool>)
            return AnimFactFromBool(value);
        else if constexpr (std::is_floating_point_v<F>)
            return AnimFactFromFloat(static_cast<float>(value));
        else if constexpr (std::is_same_v<F, GameplayTagId>)
            return value.Value;
        else
            return AnimFactFromInt(static_cast<std::int32_t>(value));
    }

    template <auto Member>
    bool ReadField(const World& world, EntityId entity, const void*, std::uint32_t& out)
    {
        using Component = typename MemberTraits<decltype(Member)>::Component;
        if (!world.IsRegistered<Component>())
            return false;
        const Component* component = world.TryGet<Component>(entity);
        if (component == nullptr)
            return false;
        out = Encode(component->*Member);
        return true;
    }
}

// A World resource binding fact slot names to gameplay values. A provider reads
// gameplay components only, never animation state, so it works with animation compiled out.
class AnimFactProviders
{
public:
    // False when the slot already has a provider: a second binding is a conflict.
    bool Bind(std::string slot, AnimFactKind kind, AnimFactReadFn read,
              const void* context = nullptr);

    // Binds a component field: BindField<&CharacterMovement::Speed>("Speed").
    template <auto Member>
    bool BindField(std::string slot)
    {
        using Traits = AnimFactProviderDetail::MemberTraits<decltype(Member)>;
        return Bind(std::move(slot), AnimFactProviderDetail::KindOf<typename Traits::Field>(),
                    &AnimFactProviderDetail::ReadField<Member>);
    }

    bool Unbind(std::string_view slot);

    // The provider's index, or -1. Indices are stable until the next Bind or
    // Unbind, which move Revision.
    [[nodiscard]] int IndexOf(std::string_view slot) const;
    [[nodiscard]] const AnimFactProvider& At(int index) const { return Providers[index]; }
    [[nodiscard]] std::size_t Size() const { return Providers.size(); }
    [[nodiscard]] std::uint64_t Revision() const { return Revision_; }

private:
    std::vector<AnimFactProvider> Providers;
    std::uint64_t Revision_ = 0;
};
