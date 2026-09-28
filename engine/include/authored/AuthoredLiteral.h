#pragma once

#include <authored/AuthoredValue.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

// Typed, never opportunistic: "3" fails an Int field rather than becoming three.
// References (assets, tags, entities) are not literals and are refused.
// Messages read "<subject> argument '<path>': ...".
bool CompileAuthoredLiteral(const JsonValue& value,
                            const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors);

// The value an unsupplied field takes: its declared default, absent when the
// field is optional, and a diagnostic when it is required.
bool CompileAuthoredDefault(const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors);
