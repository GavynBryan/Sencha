#pragma once

#include <core/identity/StrongId.h>
#include <core/metadata/NamedIdField.h>

#include <cstdint>

// Gameplay tags are symbolic gameplay facts and categories. Runtime systems
// should compare compact ids; strings belong to registration, assets, editor
// display, debugging, and tests.
using GameplayTagId = StrongId<struct GameplayTagIdTag, std::uint32_t>;

template <>
struct NamedIdField<GameplayTagId>
{
    static constexpr NamedIdKind Kind = NamedIdKind::GameplayTag;
    static const NamedIdVocabulary& Vocabulary();
};
