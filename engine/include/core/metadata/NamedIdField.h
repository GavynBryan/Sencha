#pragma once

#include <concepts>
#include <cstdint>

// An id that is a name in a vocabulary a World holds: docs/core-systems-map.md, "Named-id fields".

struct NamedIdVocabulary;

enum class NamedIdKind : std::uint8_t
{
    GameplayTag,
    DataDefinition,
    RegisteredName,
};

template <typename Id>
struct NamedIdField;

template <typename Id>
concept IsNamedIdField = requires {
    { NamedIdField<Id>::Kind } -> std::convertible_to<NamedIdKind>;
    { NamedIdField<Id>::Vocabulary() } -> std::same_as<const NamedIdVocabulary&>;
};
