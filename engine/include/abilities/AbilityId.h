#pragma once

#include <core/identity/StrongId.h>
#include <core/metadata/NamedIdField.h>

#include <cstdint>

// Compact id for an ability in one World's AbilityRegistry. Zero is invalid.
using AbilityId = StrongId<struct AbilityIdTag, std::uint32_t>;

template <>
struct NamedIdField<AbilityId>
{
    static constexpr NamedIdKind Kind = NamedIdKind::DataDefinition;
    static const NamedIdVocabulary& Vocabulary();
};
