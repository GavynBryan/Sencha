#pragma once

#include <assets/material/MaterialFormat.h>

#include <string>
#include <string_view>

class JsonValue;

//=============================================================================
// MaterialWriter
//
// The inverse of MaterialLoader: MaterialDescription back to the .smat JSON
// schema. Lives beside the parser so the two evolve with the schema together
// (the round-trip test in MaterialAssetTests is the schema lock). Fields at
// their defaults are omitted, so hand-authored minimal files stay minimal
// through an edit-save cycle.
//=============================================================================

[[nodiscard]] JsonValue WriteMaterialJson(const MaterialDescription& description);

// Writes the description to `path` as pretty-printed JSON. Returns false and
// sets *error on I/O failure.
bool SaveMaterialFile(std::string_view path,
                      const MaterialDescription& description,
                      std::string* error = nullptr);
