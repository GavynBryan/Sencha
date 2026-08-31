#pragma once

#include <assets/scene/SceneCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetStager.h>
#include <world/scene/SmapFormat.h>

#include <memory>
#include <string>

class AssetSystem;
class ComponentSerializerRegistry;
class EntityBuildPackage;

//=============================================================================
// ScenePackageBuild
//
// One cooked scene's journey from asset record to entity package across the
// async lane, shared by zone streaming (AsyncZoneLoader::BeginLoadScene) and
// runtime spawning (SceneSpawnService). Construction on the owner thread
// captures residency -- a resident scene skips the read and the parse, and
// the shared payload means the task thread never touches the cache. Build on
// the task thread stages if needed and produces the package. Settle on the
// owner thread commits freshly staged contents into the cache and reports
// success; the scene reference then belongs to the consumer, who releases it
// once its entities are the product.
//
// The value moves through the task closures; each phase runs on the thread
// its name says. Consumers own only their policy: what to stamp or read on
// the worker, and when the reference lets go.
//=============================================================================
class ScenePackageBuild
{
public:
    // Owner thread. `record` is a resolved Scene asset record; a copy is
    // kept so registry churn cannot reach the task thread.
    ScenePackageBuild(AssetSystem& assets, const AssetRecord& record);

    ScenePackageBuild(ScenePackageBuild&&) noexcept = default;
    ScenePackageBuild& operator=(ScenePackageBuild&&) noexcept = default;
    ScenePackageBuild(const ScenePackageBuild&) = delete;
    ScenePackageBuild& operator=(const ScenePackageBuild&) = delete;
    ~ScenePackageBuild();

    // Task thread: parse (or reuse the resident payload) and build the
    // package. Failure lands in Error(); the package is then absent.
    void Build(const ComponentSerializerRegistry& serializers,
               const SmapPackageOptions& options = {});

    // Valid on the task thread after a successful Build, for consumer policy
    // run beside it: stamping components into the package, reading sibling
    // cooked artifacts against the contents. Borrowed for that window only --
    // Settle() relocates the backing store, so never cache either pointer
    // across the drain (ContentsShared() is the owning form).
    [[nodiscard]] EntityBuildPackage* Package();
    [[nodiscard]] const SmapContents* Contents() const;

    // Owner thread, at the drain: commits staged contents into the cache.
    // False when the build failed or the commit refused (Error() says why);
    // any held reference is released then. True means Scene() is the held
    // reference and ContentsShared() is stable past release.
    [[nodiscard]] bool Settle();

    [[nodiscard]] std::unique_ptr<EntityBuildPackage> TakePackage();
    [[nodiscard]] std::shared_ptr<const SmapContents> ContentsShared() const;
    [[nodiscard]] SceneHandle Scene() const { return SceneRef; }
    [[nodiscard]] const std::string& Error() const { return ErrorText; }

    // Idempotent; owner thread. Safe after a failed Settle (already let go).
    void ReleaseScene();

private:
    AssetSystem* Assets = nullptr;
    AssetRecord Record;
    SceneHandle SceneRef;
    std::shared_ptr<const SmapContents> Resident;
    AssetStaging Staging; // engaged only when the scene was not resident
    bool Staged = false;
    std::unique_ptr<EntityBuildPackage> Built;
    std::string ErrorText;
};
