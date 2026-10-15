#pragma once

#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Builders for DataFieldSchema trees, so every schema states its fields the
// same way: data subtypes, authored contracts and editor operations.
namespace DataField
{
    inline DataFieldSchema Field(std::string key, DataFieldKind kind, std::string display,
                                 std::string summary, bool required = true)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        field.DisplayName = std::move(display);
        field.Summary = std::move(summary);
        field.Required = required;
        return field;
    }

    inline DataFieldSchema Enum(std::string key, std::string display, std::string summary,
                                std::vector<DataEnumChoice> choices, bool required = true)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Enum, std::move(display),
                                      std::move(summary), required);
        field.EnumChoices = std::move(choices);
        return field;
    }

    inline DataFieldSchema Vector(std::string key, std::string display, std::string summary,
                                  std::uint32_t length = 3, bool required = true)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Vector, std::move(display),
                                      std::move(summary), required);
        field.VectorLength = length;
        return field;
    }

    inline DataFieldSchema DataRef(std::string key, std::string display, std::string summary,
                                   std::string_view subtype, bool required = false)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::DataAssetRef,
                                      std::move(display), std::move(summary), required);
        field.Reference.DataSubtype = std::string(subtype);
        return field;
    }

    inline DataFieldSchema ArrayOf(std::string key, std::string display, std::string summary,
                                   DataFieldSchema element, bool required = false)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Array, std::move(display),
                                      std::move(summary), required);
        field.Children.push_back(std::move(element));
        return field;
    }

    // Present or null; `required` says whether the member may be left out.
    inline DataFieldSchema Optional(std::string key, std::string display, std::string summary,
                                    DataFieldSchema value, bool required = false)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Optional, std::move(display),
                                      std::move(summary), required);
        field.Children.push_back(std::move(value));
        return field;
    }

    inline DataFieldSchema Record(std::string key, std::string display, std::string summary,
                                  std::vector<DataFieldSchema> members, bool required = true)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Record, std::move(display),
                                      std::move(summary), required);
        field.Children = std::move(members);
        return field;
    }
}
