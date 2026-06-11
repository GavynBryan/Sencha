#pragma once

#include <assets/material/MaterialFormat.h>

#include <string>
#include <string_view>

class JsonValue;

struct MaterialParseError
{
    std::string Message;
};

//=============================================================================
// .smat parsing (docs/assets/pipeline.md, Decision L)
//
// Pure functions: no logging, no caches, no engine state — the caller logs.
// Unknown keys are errors, not warnings: a typo that silently falls back to
// a neutral default is the failure mode that fights the developer.
//=============================================================================

// Parse an already-decoded JSON document into `out`. On failure returns
// false, fills `error` if provided, and leaves `out` unspecified.
[[nodiscard]] bool ParseMaterialJson(const JsonValue& root,
                                     MaterialDescription& out,
                                     MaterialParseError* error = nullptr);

// Read + parse a .smat file from disk.
[[nodiscard]] bool LoadMaterialFromFile(std::string_view path,
                                        MaterialDescription& out,
                                        MaterialParseError* error = nullptr);
