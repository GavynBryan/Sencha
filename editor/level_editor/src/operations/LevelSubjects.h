#pragma once

#include "operations/EditorOperations.h"

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

class EditorDocument;
class EntityChangeRecorder;

// What an operation was asked to act on, in a document. Stored references are
// persistent ids; a name is a lookup convenience and may match several.
struct SubjectResolution
{
    std::vector<EntityId> Entities;
    // Empty when resolved: subject.malformed, subject.not_found, subject.ambiguous.
    std::string Code;
    std::string Message;
    // Persistent ids an ambiguous name matched, so a client can choose one.
    std::vector<std::string> Candidates;

    [[nodiscard]] bool Resolved() const { return Code.empty(); }
};

// Sixteen lowercase hex digits name an entity by persistent id, the spelling
// every entity reference uses; "@<name>" names it by its authored name.
[[nodiscard]] SubjectResolution ResolveEntitySubject(const EditorDocument& document,
                                                     std::string_view subject);

// The refusal for a subject that did not resolve, naming an ambiguous name's
// candidates in its detail.
[[nodiscard]] EditorOperationOutcome SubjectRefusal(const SubjectResolution& subject);

// Every subject of a JSON array of them, in order and each entity once; the
// first that does not resolve answers for them all.
[[nodiscard]] SubjectResolution ResolveEntitySubjects(const EditorDocument& document, const JsonValue& subjects);

// Empty when the entity carries no persistent id.
[[nodiscard]] std::string PersistentIdText(const EditorDocument& document, EntityId entity);

// Empty when the entity carries no authored name.
[[nodiscard]] std::string EntityNameText(const EditorDocument& document, EntityId entity);

// The name `text` gives `entity` (invalid for one not made yet, and a space
// when `asSpace`), as NormalizedEntityName trims it; or why it may not have
// it: a space without a name, a name too long to hold, or one shared with
// another entity where either is a space, since a space is always reachable
// as @<name>. Every operation that names something asks this.
[[nodiscard]] std::variant<std::string, EditorOperationOutcome> CheckEntityName(const EditorDocument& document,
                                                                                EntityId entity, bool asSpace,
                                                                                std::string_view text);

// Renames `entity` to `text`, or clears its name, as CheckEntityName allows.
[[nodiscard]] EditorOperationOutcome RenameEntity(EditorDocument& document, EntityChangeRecorder& recorder,
                                                  EntityId entity, std::string_view text);
