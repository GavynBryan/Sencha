#pragma once

#include <abilities/AbilityDeclaration.h>
#include <core/json/JsonValue.h>

#include <optional>
#include <string>

class VocabularyCatalog;

// A guard that answers a problem: the op it goes on, and the condition.
struct AbilityGuardFix
{
    std::string Element;
    JsonValue Condition;
    std::string Label;
};

// The guard a may-be-empty or unguarded-recipient problem asks for: a hit that
// struck, or an entity that is there and has what its argument needs. None for
// any other problem.
[[nodiscard]] std::optional<AbilityGuardFix> AbilityGuardFixFor(const JsonValue& data,
                                                                const AbilityDiagnostic& diagnostic,
                                                                const VocabularyCatalog& names);

// `fix` made: its condition joined to whatever guard the op already has.
bool ApplyAbilityGuardFix(JsonValue& data, const AbilityGuardFix& fix);
