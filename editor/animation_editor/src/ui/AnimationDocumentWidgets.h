#pragma once

#include <anim/AnimFactSchema.h>
#include <anim/AnimRigBinding.h>
#include <core/json/JsonValue.h>

#include "ui/DataForm.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Widgets report through FieldEdit so a panel knows when to preview the working
// document and when to commit one undo step.

namespace AnimationWidgets
{
[[nodiscard]] std::string Text(const JsonValue& object, std::string_view key);
[[nodiscard]] double Number(const JsonValue& object, std::string_view key, double fallback = 0.0);
void SetMember(JsonValue& object, std::string_view key, JsonValue value);
void EraseMember(JsonValue& object, std::string_view key);

// Commits when the field loses focus.
void TextMember(const char* label, JsonValue& object, std::string_view key, FieldEdit& edit, float width = 160.0f);
void NumberMember(const char* label, JsonValue& object, std::string_view key, FieldEdit& edit, float speed = 0.05f,
                  float width = 90.0f);
template <std::size_t N>
void ChoiceMember(const char* label, JsonValue& object, std::string_view key, const std::array<const char*, N>& values,
                  const std::array<const char*, N>& shown, FieldEdit& edit, float width = 70.0f)
{
    const std::string current = Text(object, key);
    std::size_t index = 0;
    while (index < N && current != values[index])
        ++index;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, index < N ? shown[index] : "?"))
    {
        for (std::size_t i = 0; i < N; ++i)
            if (ImGui::Selectable(shown[i], i == index))
            {
                SetMember(object, key, JsonValue(values[i]));
                edit |= FieldEdit::Instant();
            }
        ImGui::EndCombo();
    }
}

// Free text when `names` is empty.
void NameMember(const char* label, JsonValue& object, std::string_view key, const std::vector<std::string>& names,
                FieldEdit& edit);

struct PredicateVocabulary
{
    const AnimBoundRig* Rig = nullptr;

    std::vector<std::string> Facts() const
    {
        std::vector<std::string> names;
        if (Rig != nullptr)
            for (const AnimBoundFactSlot& slot : Rig->Slots)
                names.push_back(slot.Name);
        return names;
    }
    std::vector<std::string> Intents() const
    {
        std::vector<std::string> names;
        if (Rig != nullptr)
            for (const AnimBoundIntent& intent : Rig->Intents)
                names.push_back(intent.Name);
        return names;
    }
    std::vector<std::string> Params(const std::string& intent) const
    {
        std::vector<std::string> names;
        if (Rig != nullptr)
            for (const AnimBoundIntent& bound : Rig->Intents)
                if (bound.Name == intent)
                    for (const AnimBoundParam& param : bound.Params)
                        names.push_back(param.Name);
        return names;
    }
    AnimFactKind KindOf(const std::string& fact) const
    {
        const int slot = Rig != nullptr ? Rig->FindSlot(fact) : -1;
        return slot >= 0 ? Rig->Slots[static_cast<std::size_t>(slot)].Kind : AnimFactKind::Float;
    }
};

void DrawPredicate(JsonValue::Array& rows, const PredicateVocabulary& vocabulary, FieldEdit& edit);
}
