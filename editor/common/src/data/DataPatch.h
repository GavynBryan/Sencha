#pragma once

#include <core/json/JsonValue.h>

#include <string_view>

struct DataFieldSchema;

struct DataPatchLocation
{
    JsonValue* Value = nullptr;
    const DataFieldSchema* Field = nullptr;
};

[[nodiscard]] DataPatchLocation LocateInPatch(JsonValue& local, const JsonValue* composed, const DataFieldSchema& root,
                                              std::string_view path, bool create);

bool EraseFromPatch(JsonValue& local, const DataFieldSchema& root, std::string_view path);

bool RenameKeyedElement(JsonValue& local, const JsonValue& inherited, const DataFieldSchema& root,
                        std::string_view path, std::string_view name);
