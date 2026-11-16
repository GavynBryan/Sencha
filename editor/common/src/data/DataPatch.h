#pragma once

#include <core/json/JsonValue.h>

#include <string_view>

struct DataFieldSchema;

// A definition's own data, edited so that what it does not set stays
// inherited (docs/assets/composition.md, "Authoring in Kyusu").

struct DataPatchLocation
{
    JsonValue* Value = nullptr;
    // Describes the value; not unwrapped.
    const DataFieldSchema* Field = nullptr;
};

// The value at `path` in `local`; with `create`, what is missing is made empty
// where `composed` holds it, so the rest stays inherited, and from its default
// where nothing does. `composed` is null for a definition with no sources.
[[nodiscard]] DataPatchLocation LocateInPatch(JsonValue& local, const JsonValue* composed, const DataFieldSchema& root,
                                              std::string_view path, bool create);

// Erases the value at `path`, then every record or keyed entry that held only
// it, so the patch reads as though it had never been set. False when nothing
// was there.
bool EraseFromPatch(JsonValue& local, const DataFieldSchema& root, std::string_view path);

// Renames the keyed element at `path`, which ends at the element. Only an
// element this definition adds can be renamed: false when `inherited` holds it,
// or when this definition or its sources already have an element named `name`.
bool RenameKeyedElement(JsonValue& local, const JsonValue& inherited, const DataFieldSchema& root,
                        std::string_view path, std::string_view name);
