#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetRefCodec.h>
#include <core/composition/Composition.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A source's parsed root, or, when it has none, why: "does not exist", "is not valid JSON ...".
struct DataSourceRead
{
    const JsonValue* Root = nullptr;
    std::string Problem;
};

using DataSourceReader = std::function<DataSourceRead(std::string_view path)>;

struct DataDefinitionContext
{
    const DataAssetTypeRegistry& Types;
    const DataSchemaRegistry* Schemas = nullptr;
    const AssetPathsById& Current;
    DataSourceReader Read = {};
    std::string_view Asset = {};
    bool Provenance = false;
};

struct DataDefinitionStaging
{
    std::string TypeName;
    std::uint32_t Version = 0;
    bool Abstract = false;
    bool Composed = false;
    JsonValue Data;
    JsonValue Inherited;
    CompositionReport Composition;
    std::vector<std::string> Sources;
    std::vector<DataValidationError> Errors;
    std::shared_ptr<const void> Value;
    std::vector<AssetRef> Dependencies;

    [[nodiscard]] bool Ok() const { return Errors.empty(); }
};

[[nodiscard]] DataDefinitionStaging StageDataDefinition(const JsonValue& root, const DataDefinitionContext& context);

[[nodiscard]] bool IsAbstractDataDefinition(const JsonValue& root);

// "<path> <message>"; a message with no leading path lands on "$.data".
[[nodiscard]] DataValidationError SplitCompileError(std::string message);
