#pragma once

#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <core/metadata/EnumSchema.h>

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

// What a generated verb method may answer beyond a VerbAdmission or a VerbAnswer:
// a refusal with one of its own reasons, or a value. docs/gameplay/authored-api.md.

enum class VerbNoReason : std::uint8_t
{
    None,
};

template<>
struct EnumSchema<VerbNoReason>
{
    static constexpr std::array Values = {
        EnumValue{ VerbNoReason::None, "none", "None" },
    };
};

template<typename E>
concept AuthoredRefusalReason = std::is_enum_v<E> && HasEnumSchema<E>;

// E's zero value refuses nothing.
template<AuthoredRefusalReason E>
class VerbRefusal
{
public:
    using ReasonType = E;

    constexpr VerbRefusal(E reason)
        : Admission(VerbAnswerOf(reason))
    {
    }

    constexpr VerbRefusal(VerbAdmission status)
        : Admission(status)
    {
    }

    [[nodiscard]] constexpr VerbAnswer Answer() const { return Admission; }

private:
    VerbAnswer Admission;
};

// A value when accepted, or why not.
template<IsAuthoredResultType T, AuthoredRefusalReason E = VerbNoReason>
class VerbResult
{
public:
    using ValueType = T;
    using ReasonType = E;

    VerbResult(T value)
        : Held(std::move(value))
    {
    }

    constexpr VerbResult(E reason)
        : Admission(VerbAnswerOf(reason))
    {
    }

    constexpr VerbResult(VerbAdmission status)
        : Admission(status)
    {
    }

    [[nodiscard]] constexpr VerbAnswer Answer() const { return Admission; }
    [[nodiscard]] constexpr const std::optional<T>& Value() const { return Held; }

private:
    VerbAnswer Admission;
    std::optional<T> Held;
};
