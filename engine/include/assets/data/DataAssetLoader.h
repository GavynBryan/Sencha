#pragma once

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>
#include <core/metadata/DataSchema.h>

#include <memory>
#include <string>

class LoggingProvider;

struct CompiledDataAsset
{
    std::string TypeName;
    uint32_t Version = 0;
    std::shared_ptr<const void> Value;
};

// Stages a .sdata file: parse, check the envelope, validate `data` against the
// subtype's authoring schema, then hand it to the subtype's compiler. What the
// compiler returns is opaque here; only the subtype knows its own value type,
// which is what lets a game module add one without an engine edit.
class DataAssetLoader final : public IAssetStager
{
public:
    DataAssetLoader(LoggingProvider& logging,
                    DataAssetTypeRegistry* types,
                    DataSchemaRegistry* schemas,
                    DataAssetCache* cache);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    [[nodiscard]] DataAssetHandle CommitTyped(AssetStaging&& staged);
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    Logger& Log;
    DataAssetTypeRegistry* Types = nullptr;
    DataSchemaRegistry* Schemas = nullptr;
    DataAssetCache* Cache = nullptr;
};
