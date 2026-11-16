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

// A data definition's source by asset path: the file at runtime, an editor's
// working copy in Kyusu. Null when there is no such asset.
using DataSourceReader = std::function<const JsonValue*(std::string_view path)>;

struct DataDefinitionContext
{
    const DataAssetTypeRegistry& Types;
    const DataSchemaRegistry* Schemas = nullptr;
    const AssetPathsById& Current;
    // Needed only by a definition that extends or includes another.
    DataSourceReader Read;
    // The definition's own path: what its diagnostics and provenance name.
    std::string_view Asset;
    bool Provenance = false;
};

struct DataDefinitionStaging
{
    std::string TypeName;
    std::uint32_t Version = 0;
    bool Abstract = false;
    bool Composed = false;
    // The composed `data`.
    JsonValue Data;
    CompositionReport Composition;
    // Every definition composition read, bases and fragments, transitively.
    std::vector<std::string> Sources;
    // Paths are the envelope's ("$.data.mods{m1}.magnitude"); a keyed element is
    // named by key once the definition composes.
    std::vector<DataValidationError> Errors;
    std::shared_ptr<const void> Value;
    std::vector<AssetRef> Dependencies;

    [[nodiscard]] bool Ok() const { return Errors.empty(); }
};

// Checks the envelope, composes the definition over its base and fragments,
// validates the result (partially when abstract), and compiles it (unless
// abstract). The one path both the runtime loader and Kyusu stage through.
[[nodiscard]] DataDefinitionStaging StageDataDefinition(const JsonValue& root, const DataDefinitionContext& context);

// Whether an envelope declares itself abstract, without staging it.
[[nodiscard]] bool IsAbstractDataDefinition(const JsonValue& root);

// A compiler's "<path> <message>" split, the path landing the error on its field;
// a message with no leading path belongs to the definition as a whole ("$.data").
[[nodiscard]] DataValidationError SplitCompileError(std::string message);
