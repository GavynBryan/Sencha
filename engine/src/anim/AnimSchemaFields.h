#pragma once

#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Builders for the authoring schemas of the animation data subtypes. Private to
// the animation sources: every animation asset states its fields the same way,
// and nothing outside them builds schemas this way.
namespace AnimSchema
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

    inline DataFieldSchema Record(std::string key, std::string display, std::string summary,
                                  std::vector<DataFieldSchema> members, bool required = true)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Record, std::move(display),
                                      std::move(summary), required);
        field.Children = std::move(members);
        return field;
    }
}
