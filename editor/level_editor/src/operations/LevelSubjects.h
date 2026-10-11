#pragma once

#include <ecs/EntityId.h>

#include <string>
#include <string_view>
#include <vector>

class EditorDocument;

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

// Empty when the entity carries no persistent id.
[[nodiscard]] std::string PersistentIdText(const EditorDocument& document, EntityId entity);

// Empty when the entity carries no authored name.
[[nodiscard]] std::string EntityNameText(const EditorDocument& document, EntityId entity);
