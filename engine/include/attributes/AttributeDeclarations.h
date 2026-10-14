#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

class AttributeRegistry;

// Attributes written down as content, registered when content is published, so a
// game declares Ammo or Energy without a line of C++.

inline constexpr std::string_view kAttributeDeclarationsType = "gameplay.attribute_declarations";

struct AttributeDeclaration
{
    std::string Name;
    float Min = std::numeric_limits<float>::lowest();
    float Max = std::numeric_limits<float>::max();
    float Default = 0.0f;
};

struct AttributeDeclarations
{
    std::vector<AttributeDeclaration> Attributes;
};

void RegisterAttributeDeclarations(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// Registers each declared attribute. One already registered with the same range
// and default is fine; a different one is an error, and the first stands.
void DeclareAttributes(const AttributeDeclarations& declarations, AttributeRegistry& attributes,
                       std::vector<std::string>& errors);
