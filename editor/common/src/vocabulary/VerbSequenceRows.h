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

// An authored verb sequence ({"conditions": [...], "commands": [...]}) laid out
// as properties, and the edits those properties make. Rows come from the
// vocabulary: a command's arguments are its verb's contract, each from a
// constant, one of the producer's ports, or an entity named by identity. Pure
// over JSON, so whatever holds a sequence -- a trigger, a later producer --
// edits it the same way and commits the result as one undoable step.
//
// Keys: "conditions" and "commands" are the lists; "c<i>" and "k<i>" their
// entries; "k<i>.verb", "k<i>.on_failure", "k<i>.arg.<name>.source|value|frame",
// "c<i>.entity|has|tags.all|tags.any|tags.none|query|expect",
// "c<i>.arg.<name>.source|value".
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
