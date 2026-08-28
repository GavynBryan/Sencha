#pragma once

#include <anim/AnimationClipHandle.h>
#include <anim/SkeletonHandle.h>
#include <assets/animation/AnimationClipAssetLoader.h>
#include <assets/audio_clip/AudioClipAssetLoader.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/skeleton/SkeletonAssetLoader.h>
#include <assets/skinned_mesh/SkinnedMeshAssetLoader.h>
#include <assets/static_mesh/StaticMeshAssetLoader.h>
#include <assets/texture/TextureAssetLoader.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/logging/Logger.h>
#include <render/Material.h>
#include <render/MaterialSetCache.h>
#include <render/TextureHandle.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <assets/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

class AnimationClipCache;
class AudioClipCache;
class ComponentSerializerRegistry;
class MaterialCache;
class LoggingProvider;
class SceneCache;
class SkeletonCache;
class SkinnedMeshCache;
class StaticMeshCache;
class TextureCache;

//=============================================================================
// AssetSystem
//
// The asset front door. Load* resolves a virtual path through the registry
// and composes the type's staged loader (docs/assets/pipeline.md, Decision
// C): dedup check, LoadStaged, CommitTyped — back-to-back on the owner
// thread here; split across the async lane by the manifest-driven zone path
// (Stage 3). One code path, two schedulings.
//=============================================================================
class AssetSystem
{
public:
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache& meshes,
                MaterialCache& materials,
                TextureCache& textures,
                AudioClipCache& audioClips,
                SkeletonCache& skeletons,
                AnimationClipCache& animationClips,
                SkinnedMeshCache& skinnedMeshes,
                MaterialSetCache& materialSets,
                SceneCache& scenes,
                const ComponentSerializerRegistry& sceneSerializers);
    // `sceneSerializers` is the component vocabulary scene staging validates
    // against; without it the scene kind stages every load as a failure,
    // which is a capability this composition was built without.
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache* meshes,
                MaterialCache* materials,
                TextureCache* textures = nullptr,
                AudioClipCache* audioClips = nullptr,
                SkeletonCache* skeletons = nullptr,
                AnimationClipCache* animationClips = nullptr,
                SkinnedMeshCache* skinnedMeshes = nullptr,
                MaterialSetCache* materialSets = nullptr,
                SceneCache* scenes = nullptr,
                const ComponentSerializerRegistry* sceneSerializers = nullptr);

    [[nodiscard]] StaticMeshHandle LoadStaticMesh(std::string_view path);
    [[nodiscard]] SkinnedMeshHandle LoadSkinnedMesh(std::string_view path);
    [[nodiscard]] MaterialHandle LoadMaterial(std::string_view path);

    // `srgb` applies only when the source is loose image bytes (Stage 1 PNG
    // mapping) and only on first load; cooked .stex carries a usage tag that
    // replaces this parameter (docs/assets/pipeline.md, Decisions E/L).
    [[nodiscard]] TextureHandle LoadTexture(std::string_view path, bool srgb = true);

    [[nodiscard]] AudioClipHandle LoadAudioClip(std::string_view path);

    [[nodiscard]] SkeletonHandle LoadSkeleton(std::string_view path);
    [[nodiscard]] AnimationClipHandle LoadAnimationClip(std::string_view path);

    // Resolves and loads a cooked scene into residency (refcount 1, owned by
    // the caller). A resident path gains a reference instead of re-parsing.
    [[nodiscard]] SceneHandle LoadScene(std::string_view path);
    [[nodiscard]] SceneHandle TryAcquireScene(std::string_view path);
    void ReleaseScene(SceneHandle handle);

    // The scene load's two halves for callers that split them across the
    // async lane themselves (zone streaming): StageScene is task-thread-safe
    // by the staging contract, CommitScene is owner-thread and returns the
    // resident handle (refcount 1, owned by the caller).
    [[nodiscard]] AssetStaging StageScene(const AssetRecord& record);
    [[nodiscard]] SceneHandle CommitScene(AssetStaging&& staged);

    // Resident contents for a held handle; nullptr once released. The shared
    // form is for consumers that read beyond the resolving drain callback.
    [[nodiscard]] const SmapContents* GetSceneContents(SceneHandle handle) const;
    [[nodiscard]] std::shared_ptr<const SmapContents>
    GetSceneContentsShared(SceneHandle handle) const;

    [[nodiscard]] StaticMeshHandle RegisterProceduralStaticMesh(std::string_view path, MeshGeometry mesh);
    [[nodiscard]] MaterialHandle RegisterProceduralMaterial(std::string_view path, Material material);

    // Per-section material binding (the instance-level material set a
    // StaticMeshComponent references). Acquire deduplicates by content and
    // returns an invalid handle when no MaterialSetCache is wired. Get exposes
    // the ordered members for extraction; Release drops a reference.
    [[nodiscard]] MaterialSetHandle AcquireMaterialSet(std::span<const MaterialHandle> materials);
    [[nodiscard]] const std::vector<MaterialHandle>* GetMaterialSet(MaterialSetHandle handle) const;
    void ReleaseMaterialSet(MaterialSetHandle handle);

    [[nodiscard]] std::string_view GetPathForStaticMesh(StaticMeshHandle handle) const;
    [[nodiscard]] std::string_view GetPathForTexture(TextureHandle handle) const;
    [[nodiscard]] std::string_view GetPathForSkinnedMesh(SkinnedMeshHandle handle) const;
    [[nodiscard]] std::string_view GetPathForMaterial(MaterialHandle handle) const;
    [[nodiscard]] std::string_view GetPathForAudioClip(AudioClipHandle handle) const;
    [[nodiscard]] std::string_view GetPathForSkeleton(SkeletonHandle handle) const;
    [[nodiscard]] std::string_view GetPathForAnimationClip(AnimationClipHandle handle) const;

    [[nodiscard]] const AssetRecord* Resolve(std::string_view path, AssetType expectedType) const;

    // True if `path` currently has a live entry in the cache for `type`,
    // without touching its refcount. The hot-reload driver uses this to skip
    // re-staging assets that aren't loaded (Stage 6).
    [[nodiscard]] bool IsResident(std::string_view path, AssetType type) const;

    // Id-first ref resolution (Decision A / Stage 4e): when the registry
    // knows the id, the record's current path wins — that is what makes an
    // id-stamped ref survive a rename the stamped path predates. An unknown
    // id (or a type mismatch, which is logged) falls back to the stamped
    // path; an invalid id is simply "no id", not an error, so path-only
    // refs flow through unchanged.
    [[nodiscard]] std::string_view ResolveRefPath(AssetId id,
                                                  std::string_view fallbackPath,
                                                  AssetType expectedType) const;

    // Cached-only acquisition: a ref-counted handle if the asset is already
    // resident, an invalid handle otherwise. Never loads, never logs — the
    // preload path uses these to dedup against the caches before submitting
    // staged work.
    [[nodiscard]] StaticMeshHandle TryAcquireStaticMesh(std::string_view path);
    [[nodiscard]] SkinnedMeshHandle TryAcquireSkinnedMesh(std::string_view path);
    [[nodiscard]] MaterialHandle TryAcquireMaterial(std::string_view path);
    [[nodiscard]] TextureHandle TryAcquireTexture(std::string_view path);
    [[nodiscard]] AudioClipHandle TryAcquireAudioClip(std::string_view path);
    [[nodiscard]] SkeletonHandle TryAcquireSkeleton(std::string_view path);
    [[nodiscard]] AnimationClipHandle TryAcquireAnimationClip(std::string_view path);

    // Release counterparts to Load*/TryAcquire* — thin forwards to the caches
    // so callers that hold raw handles don't need cache access to let go.
    void ReleaseStaticMesh(StaticMeshHandle handle);
    void ReleaseSkinnedMesh(SkinnedMeshHandle handle);
    void ReleaseMaterial(MaterialHandle handle);
    void ReleaseTexture(TextureHandle handle);
    void ReleaseAudioClip(AudioClipHandle handle);
    void ReleaseSkeleton(SkeletonHandle handle);
    void ReleaseAnimationClip(AnimationClipHandle handle);

    // Whether this process can hold a loaded asset of `type` at all.
    //
    // A composition query, never a health check. Which caches exist is fixed by
    // the constructor arguments and never changes afterwards: RegisterKinds
    // attaches a kind's store only for a cache that was passed, and nothing
    // detaches one later. So a false answer means this process was built
    // without that capability -- a headless host with no graphics services has
    // no mesh or texture cache -- and never that a cache failed or went away.
    //
    // That is what lets a consumer treat an unresolvable asset of an
    // unsupported kind as expected rather than as an error, while an
    // unresolvable asset of a supported kind stays a real failure.
    [[nodiscard]] bool HasStore(AssetType type) const;

    // Type-erased residency, for drivers that hold a reference without naming
    // the handle type. Invalid lease when the kind is unregistered or the
    // asset is not resident; never loads.
    [[nodiscard]] AssetLease TryAcquireLease(std::string_view path, AssetType type);

    // Owner-thread commit of a staged payload, dispatched through the kind's
    // registered Commit. Returns the creation reference.
    [[nodiscard]] AssetLease Commit(AssetStaging&& staged);

    // The staged-load surface (Decision C), exposed for async drivers: the
    // preloader runs LoaderFor(type)->LoadStaged on a task thread against
    // DefaultSource(), and commits at the drain point.
    [[nodiscard]] IAssetStager* LoaderFor(AssetType type);
    [[nodiscard]] IAssetSource& DefaultSource() { return Source; }

    // The registered outer kinds. Scanning, preload, and hot reload read this
    // instead of carrying their own switch over AssetType. Mutable so a game
    // module can register a kind of its own at composition time.
    [[nodiscard]] AssetKindRegistry& Kinds() { return KindRegistry; }
    [[nodiscard]] const AssetKindRegistry& Kinds() const { return KindRegistry; }

    // Synchronous typed facades still name their own loader; only generic
    // orchestration goes through the registry.
    [[nodiscard]] StaticMeshAssetLoader& StaticMeshLoaderRef() { return MeshLoader; }
    [[nodiscard]] TextureAssetLoader& TextureLoaderRef() { return TexLoader; }
    [[nodiscard]] MaterialAssetLoader& MaterialLoaderRef() { return MatLoader; }
    [[nodiscard]] AudioClipAssetLoader& AudioClipLoaderRef() { return ClipLoader; }

private:
    void RegisterKinds();

    [[nodiscard]] IAssetStore* StoreFor(AssetType type);
    [[nodiscard]] const IAssetStore* StoreFor(AssetType type) const;

    Logger& Log;
    AssetRegistry& Registry;
    StaticMeshCache* StaticMeshes = nullptr;
    MaterialCache* Materials = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
    TextureCache* Textures = nullptr;
    AudioClipCache* AudioClips = nullptr;
    SkeletonCache* Skeletons = nullptr;
    AnimationClipCache* AnimationClips = nullptr;
    SkinnedMeshCache* SkinnedMeshes = nullptr;
    SceneCache* Scenes = nullptr;

    FileAssetSource Source;
    StaticMeshAssetLoader MeshLoader;
    TextureAssetLoader TexLoader;
    MaterialAssetLoader MatLoader;
    AudioClipAssetLoader ClipLoader;
    SkeletonAssetLoader SkelLoader;
    AnimationClipAssetLoader AnimLoader;
    SkinnedMeshAssetLoader SkinnedLoader;
    SceneAssetLoader SceneLoader;
    AssetKindRegistry KindRegistry;
};
