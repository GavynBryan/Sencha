#pragma once

// Edits as the animation editor's form and panels commit them: a copy of the
// document's root changed and applied as one undo step.

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/DataForm.h"

#include <gtest/gtest.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>

inline void AuthorDocument(AnimationPreviewWorkspace& workspace, const std::string& path,
                           const std::function<void(JsonValue& data)>& edit)
{
    ASSERT_TRUE(workspace.OpenAnimationDocument(path)) << path << ": " << workspace.DocumentError;
    DataDocument& document = *workspace.FindDocument(path);
    JsonValue root = document.CopyRoot();
    edit(*root.Find("data"));
    ApplyFieldEdit(document, workspace, FieldEdit::Instant(), std::move(root));
}

inline JsonValue JsonObjectOf(std::initializer_list<std::pair<const char*, JsonValue>> members)
{
    JsonValue::Object object;
    for (const auto& [key, value] : members)
        object.emplace_back(key, value);
    return JsonValue(std::move(object));
}

// The array member `key`, added empty when the object has none.
inline JsonValue::Array& JsonArrayOf(JsonValue& object, const char* key)
{
    if (object.Find(key) == nullptr)
        object.AsObject().emplace_back(key, JsonValue(JsonValue::Array{}));
    return object.Find(key)->AsArray();
}
