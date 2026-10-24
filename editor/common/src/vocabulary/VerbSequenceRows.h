#pragma once

#include "properties/PropertyNode.h"

#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <core/json/JsonValue.h>
#include <logic/VerbSequence.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// An authored verb sequence as properties laid out from the vocabulary, and the
// new sequence each edit makes. Pure over JSON, so any holder of a sequence
// edits it alike; keys are listed in docs/gameplay/triggers.md, "Inspector keys".
struct VerbSequenceVocabulary
{
    const VerbRegistry* Verbs = nullptr;
    const AuthoredQueryRegistry* Queries = nullptr;
    std::span<const SequencePort> Ports;
};

[[nodiscard]] std::vector<PropertyNode> VerbSequenceRows(const JsonValue& sequence,
                                                         const VerbSequenceVocabulary& vocabulary,
                                                         std::span<const std::string> problems);

// The sequence with `key` set to `value`; none when the row takes no such value.
[[nodiscard]] std::optional<JsonValue> SetVerbSequenceValue(const JsonValue& sequence,
                                                            const VerbSequenceVocabulary& vocabulary,
                                                            std::string_view key, std::string_view value);

// The sequence after a list verb: "add" (with the verb or condition chosen),
// "remove", "up", "down".
[[nodiscard]] std::optional<JsonValue> ActOnVerbSequence(const JsonValue& sequence,
                                                         const VerbSequenceVocabulary& vocabulary,
                                                         std::string_view key, std::string_view verb,
                                                         std::string_view choice);
