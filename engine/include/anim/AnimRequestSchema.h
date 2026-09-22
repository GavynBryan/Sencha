#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class AnimRequestParamKind : std::uint8_t { Float, Int, Bool, Tag };

struct AnimRequestParamDeclaration
{
    std::string Name;
    AnimRequestParamKind Kind = AnimRequestParamKind::Float;
};

struct AnimRequestIntentDeclaration
{
    // Portable names only. A World's bound view resolves tags against its vocabulary.
    std::string Intent;
    std::array<AnimRequestParamDeclaration, 4> Params;
    std::uint8_t ParamCount = 0;
};

struct AnimRequestSchema
{
    std::vector<AnimRequestIntentDeclaration> Intents;
};

inline constexpr std::string_view kAnimRequestSchemaType = "animation.request_schema";

void RegisterAnimRequestSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
