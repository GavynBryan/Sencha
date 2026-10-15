#pragma once

#include <abilities/AbilityDeclaration.h>

#include <array>
#include <optional>
#include <string_view>

// The persisted names of a binding's choices, shared by every form that writes one:
// an ability's input_default, a grant entry in a scene, an effect's granted ability.
template<typename E>
struct AbilityNamed
{
    std::string_view Name;
    E Value;
};

inline constexpr std::array<AbilityNamed<AbilityInputPolicy>, 3> kAbilityInputPolicyNames{ {
    { "press", AbilityInputPolicy::Press },
    { "press_release", AbilityInputPolicy::PressRelease },
    { "toggle", AbilityInputPolicy::Toggle },
} };

inline constexpr std::array<AbilityNamed<AbilityConsume>, 2> kAbilityConsumeNames{ {
    { "exclusive", AbilityConsume::Exclusive },
    { "observe", AbilityConsume::Observe },
} };

inline constexpr std::array<AbilityNamed<AbilityWhenBlocked>, 2> kAbilityWhenBlockedNames{ {
    { "consume", AbilityWhenBlocked::Consume },
    { "fall_through", AbilityWhenBlocked::FallThrough },
} };

inline constexpr std::array<AbilityNamed<AbilityInterrupt>, 2> kAbilityInterruptNames{ {
    { "cancel", AbilityInterrupt::Cancel },
    { "release", AbilityInterrupt::Release },
} };

template<typename E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> FindAbilityNamed(const std::array<AbilityNamed<E>, N>& names,
                                                          std::string_view name)
{
    for (const AbilityNamed<E>& entry : names)
        if (entry.Name == name)
            return entry.Value;
    return std::nullopt;
}

template<typename E, std::size_t N>
[[nodiscard]] constexpr std::string_view AbilityNameOf(const std::array<AbilityNamed<E>, N>& names, E value)
{
    for (const AbilityNamed<E>& entry : names)
        if (entry.Value == value)
            return entry.Name;
    return {};
}
