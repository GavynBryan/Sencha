#pragma once

#include <assets/scene/SceneCache.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>

class ComponentSerializerRegistry;
class LoggingProvider;

//=============================================================================
// SceneAssetLoader
//
// Staged-load contract for cooked scenes. Stage: bytes -> validated
// SmapContents, every component fingerprint-checked against the serializer
// registry, so schema skew refuses on the task thread with the component
// named. Commit: register with SceneCache. Payload type: SmapContents.
//
// The stage half declares no dependency edges: a .smap's dependency table
// carries paths whose types only the registry knows at load, while staged
// dependencies are typed by contract. Dependency warming therefore goes
// through AssetPreload over ResolveSmapDependencyPaths, exactly as the zone
// path drives it.
//=============================================================================
class SceneAssetLoader final : public IAssetStager
{
public:
    SceneAssetLoader(LoggingProvider& logging,
                     SceneCache* cache,
                     const ComponentSerializerRegistry* serializers);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller).
    [[nodiscard]] SceneHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    SceneCache* Cache = nullptr;
    const ComponentSerializerRegistry* Serializers = nullptr;
};
