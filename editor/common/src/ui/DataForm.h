#pragma once

#include "data/DataFieldEdit.h"

#include <core/json/JsonValue.h>

#include <string>
#include <string_view>
#include <vector>

// The schema-generated form for one field and everything under it. A
// purpose-built surface delegates here for what it has nothing better for.
[[nodiscard]] FieldEdit DrawDataField(JsonValue& value, const DataFieldSchema& field, const std::string& path,
                                      DataFormHost& host);

// Tooltip on hover, and SelectField on click. Call right after the widget.
void DrawDataFieldHelp(DataFormHost& host, const DataFieldSchema& field, std::string_view path);
