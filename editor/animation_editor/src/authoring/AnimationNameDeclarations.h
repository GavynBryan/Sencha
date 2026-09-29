#pragma once

#include <anim/AnimDiagnostic.h>
#include <core/json/JsonValue.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct DataFieldSchema;
class GameplayTagRegistry;

// The value at a diagnostic's "$.data.layers[1].idle" path, and the schema
// field describing it; both null where the path leaves the document.
struct AnimationFieldAt
{
    const JsonValue* Value = nullptr;
    const DataFieldSchema* Field = nullptr;
};

[[nodiscard]] AnimationFieldAt FindAnimationField(const JsonValue& root, const DataFieldSchema& data,
                                                  std::string_view path);

// A document's root and its data schema, by asset path; a null root when the
// asset cannot be read.
struct AnimationDocumentView
{
    const JsonValue* Root = nullptr;
    const DataFieldSchema* Data = nullptr;
};

// The gameplay-tag names diagnostics point at that `tags` does not know, in
// the order first reported: what a tag declaration would have to add.
[[nodiscard]] std::vector<std::string> UndeclaredAnimationNames(
    const std::vector<AnimDiagnostic>& diagnostics, const GameplayTagRegistry& tags,
    const std::function<AnimationDocumentView(std::string_view)>& document);

// Appends each name a gameplay.tag_declarations root does not list yet.
bool AddAnimationTagDeclarations(JsonValue& root, const std::vector<std::string>& names);
