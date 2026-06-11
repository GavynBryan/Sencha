#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <imgui.h>

#include <string>
#include <tuple>
#include <type_traits>

//=============================================================================
// SchemaWidgets
//
// Reflection-driven ImGui widgets. DrawSchemaFields<T> iterates a type's
// TypeSchema fields and renders an appropriate widget per member: drag
// scalars for arithmetic types and vectors, checkboxes for bools, combos for
// enums with an EnumSchema, and nested tree sections for schema-bearing
// struct members.
//=============================================================================

struct SchemaWidgetResult
{
    bool Changed = false;   // a widget modified the value this frame
    bool Committed = false; // an edit finished this frame (drag released, combo picked, ...)

    SchemaWidgetResult& operator|=(const SchemaWidgetResult& other)
    {
        Changed |= other.Changed;
        Committed |= other.Committed;
        return *this;
    }
};

template <typename T>
struct IsVecTrait : std::false_type
{
};

template <int N, typename T>
struct IsVecTrait<Vec<N, T>> : std::true_type
{
};

template <typename T>
struct IsQuatTrait : std::false_type
{
};

template <typename T>
struct IsQuatTrait<Quat<T>> : std::true_type
{
};

template <typename T>
constexpr ImGuiDataType ImGuiDataTypeFor()
{
    if constexpr (std::is_same_v<T, float>)
        return ImGuiDataType_Float;
    else if constexpr (std::is_same_v<T, double>)
        return ImGuiDataType_Double;
    else if constexpr (std::is_signed_v<T>)
        return sizeof(T) == 8 ? ImGuiDataType_S64 : ImGuiDataType_S32;
    else
        return sizeof(T) == 8 ? ImGuiDataType_U64 : ImGuiDataType_U32;
}

template <typename T>
SchemaWidgetResult DrawSchemaFields(T& value);

template <typename T>
SchemaWidgetResult DrawSchemaValue(const char* label, T& value)
{
    SchemaWidgetResult result;

    if constexpr (std::is_same_v<T, bool>)
    {
        if (ImGui::Checkbox(label, &value))
        {
            result.Changed = true;
            result.Committed = true;
        }
    }
    else if constexpr (std::is_enum_v<T> && HasEnumSchema<T>)
    {
        const char* current = "<unknown>";
        for (const auto& entry : EnumSchema<T>::Values)
        {
            if (entry.Value == value)
                current = entry.Name.data();
        }

        if (ImGui::BeginCombo(label, current))
        {
            for (const auto& entry : EnumSchema<T>::Values)
            {
                const bool selected = entry.Value == value;
                if (ImGui::Selectable(entry.Name.data(), selected) && !selected)
                {
                    value = entry.Value;
                    result.Changed = true;
                    result.Committed = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    else if constexpr (std::is_arithmetic_v<T>)
    {
        result.Changed = ImGui::DragScalar(label, ImGuiDataTypeFor<T>(), &value, 0.05f);
        result.Committed = ImGui::IsItemDeactivatedAfterEdit();
    }
    else if constexpr (IsVecTrait<T>::value)
    {
        using Component = std::decay_t<decltype(value[0])>;
        constexpr int N = T::Dimensions;

        Component components[N];
        for (int i = 0; i < N; ++i)
            components[i] = value[i];

        if (ImGui::DragScalarN(label, ImGuiDataTypeFor<Component>(), components, N, 0.05f))
        {
            for (int i = 0; i < N; ++i)
                value[i] = components[i];
            result.Changed = true;
        }
        result.Committed = ImGui::IsItemDeactivatedAfterEdit();
    }
    else if constexpr (IsQuatTrait<T>::value)
    {
        using Component = std::decay_t<decltype(value.X)>;
        Component components[4] = { value.X, value.Y, value.Z, value.W };

        if (ImGui::DragScalarN(label, ImGuiDataTypeFor<Component>(), components, 4, 0.01f))
        {
            value.X = components[0];
            value.Y = components[1];
            value.Z = components[2];
            value.W = components[3];
            result.Changed = true;
        }
        result.Committed = ImGui::IsItemDeactivatedAfterEdit();
    }
    else if constexpr (HasTypeSchema<T>)
    {
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
        {
            result = DrawSchemaFields(value);
            ImGui::TreePop();
        }
    }
    else
    {
        ImGui::Text("%s: <unsupported type>", label);
    }

    return result;
}

template <typename T>
SchemaWidgetResult DrawSchemaFields(T& value)
{
    SchemaWidgetResult result;

    std::apply(
        [&](auto&&... fields)
        {
            (
                [&]
                {
                    const std::string label(fields.Name);
                    result |= DrawSchemaValue(label.c_str(), value.*(fields.Ptr));
                }(),
                ...);
        },
        TypeSchema<T>::Fields());

    return result;
}
