#include "SceneThumbnailCache.h"

#include "SceneRenderQueueBuilder.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include "scene_source/SceneSourcePaths.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/logging/Logger.h>
#include <math/geometry/3d/AabbTransform.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <algorithm>

namespace
{
    // The bounds the thumbnail frames: what actually DRAWS -- brush bodies,
    // placed meshes, skinned meshes. Lights, markers, and bare transforms are
    // invisible, and letting them into the frame is what once made every cell
    // fill by a different amount.
    [[nodiscard]] Aabb3d RenderableBounds(const EditorDocument& document,
                                          StaticMeshCache& meshes,
                                          SkinnedMeshCache* skinned)
    {
        const EditorScene& scene = document.GetScene();
        const World& world = document.GetRegistry().Components;
        Aabb3d bounds = Aabb3d::Empty();
        for (EntityId entity : scene.GetAllEntities())
        {
            if (const std::optional<Aabb3d> brush = scene.TryGetWorldBounds(entity))
            {
                bounds.ExpandToInclude(*brush);
                continue;
            }
            const Transform3f* transform = scene.TryGetWorldTransform(entity);
            if (transform == nullptr)
                continue;
            if (const auto* placed = world.TryGet<StaticMeshComponent>(entity))
                if (const GpuStaticMesh* mesh = meshes.Get(placed->Mesh))
                    bounds.ExpandToInclude(
                        TransformAabb(mesh->LocalBounds, transform->ToMat4()));
            if (const auto* rig = world.TryGet<SkinnedMeshComponent>(entity);
                rig != nullptr && skinned != nullptr)
                if (const GpuStaticMesh* mesh = skinned->Get(rig->Mesh))
                    bounds.ExpandToInclude(
                        TransformAabb(mesh->LocalBounds, transform->ToMat4()));
        }
        // A scene with nothing renderable (markers only) frames its entities'
        // positions so the cell is at least honest about where things sit.
        if (!bounds.IsValid())
        {
            for (EntityId entity : scene.GetAllEntities())
                if (const Transform3f* transform = scene.TryGetWorldTransform(entity))
                    bounds.ExpandToInclude(Aabb3d::FromMinMax(
                        transform->Position - Vec3d::One() * 0.5f,
                        transform->Position + Vec3d::One() * 0.5f));
        }
        if (!bounds.IsValid())
            bounds = Aabb3d::FromMinMax(Vec3d::One() * -0.5f, Vec3d::One() * 0.5f);
        return bounds;
    }
} // namespace

SceneThumbnailCache::SceneThumbnailCache(ThumbnailStudio& studio,
                                         RuntimeAssets& assets,
                                         StaticMeshCache& meshes,
                                         LoggingProvider& logging)
    : Studio(studio)
    , Assets(assets)
    , Meshes(meshes)
    , Logging(logging)
{
}

SceneThumbnailCache::~SceneThumbnailCache() = default;

void SceneThumbnailCache::SetContentRoots(std::vector<std::filesystem::path> roots)
{
    ContentRoots = std::move(roots);
}

void SceneThumbnailCache::BeginFrame()
{
    ++FrameClock;
}

ImTextureID SceneThumbnailCache::Thumbnail(const std::string& assetPath)
{
    Entry& entry = Entries[assetPath];
    if (!entry.Target.IsValid())
    {
        entry.Target = Studio.AllocateTarget();
        entry.RemainingPasses = ThumbnailStudio::PassesPerTarget();
    }
    entry.LastUsedFrame = FrameClock;
    return Studio.Display(entry.Target);
}

void SceneThumbnailCache::AppendLiveViewports(std::vector<ViewportId>& live) const
{
    for (const auto& [path, entry] : Entries)
        if (entry.Target.IsValid())
            live.push_back(entry.Target);
}

bool SceneThumbnailCache::LoadEntry(const std::string& assetPath, Entry& entry)
{
    entry.Document = std::make_unique<EditorDocument>(Logging);
    entry.Document->SetContentRoots(ContentRoots);
    entry.Document->SetAssetEnvironment(Assets);

    const std::filesystem::path file =
        ResolveSceneSourceFile(ContentRoots, assetPath);
    if (file.empty() || !entry.Document->Load(file.generic_string()))
    {
        Logging.GetLogger<SceneThumbnailCache>().Warn(
            "scene thumbnail: could not load '{}'", assetPath);
        return false;
    }

    entry.Document->GetScene().RefreshDerivedTransforms();
    entry.Camera = ThumbnailStudio::FrameSubject(
        RenderableBounds(*entry.Document, Meshes, Assets.SkinnedMeshes.get()));

    entry.Queues = std::make_unique<SceneRenderQueueBuilder>(
        Assets.Assets, *Assets.StaticMeshes, Assets.Materials, Assets.MaterialSets,
        Logging, nullptr, Assets.SkinnedMeshes.get());
    entry.Queues->Build(*entry.Document);
    return true;
}

void SceneThumbnailCache::RenderPending(const FrameContext& frame)
{
    // Scratch payloads whose recorded passes are safely retired go first: the
    // preview keeps only its target once every slot holds the image.
    for (auto& [path, entry] : Entries)
        if (entry.Document != nullptr && FrameClock > entry.ReleasePayloadAfter)
        {
            entry.Queues.reset();
            entry.Document.reset();
        }

    // One pass per frame keeps the editor responsive while a folder of scenes
    // streams in; each thumbnail needs a pass per in-flight slot before every
    // displayed frame shows it.
    Entry* pending = nullptr;
    std::string pendingPath;
    for (auto& [path, entry] : Entries)
        if (entry.RemainingPasses > 0 && !entry.Failed
            && (pending == nullptr || entry.LastUsedFrame > pending->LastUsedFrame))
        {
            pending = &entry;
            pendingPath = path;
        }
    if (pending == nullptr)
        return;

    if (!pending->Loaded)
    {
        if (!LoadEntry(pendingPath, *pending))
        {
            pending->Failed = true;
            pending->RemainingPasses = 0;
            return;
        }
        pending->Loaded = true;
    }

    const RenderQueue* queues[] = { &pending->Queues->BrushQueue(),
                                    &pending->Queues->MeshQueue() };
    if (!Studio.RenderPass(frame, pending->Target, pending->Camera, queues))
        return;

    if (--pending->RemainingPasses == 0)
        pending->ReleasePayloadAfter =
            FrameClock + static_cast<std::uint64_t>(ThumbnailStudio::PassesPerTarget());
}

void SceneThumbnailCache::TrimToBudget(std::size_t budget)
{
    while (Entries.size() > budget)
    {
        auto oldest = Entries.begin();
        for (auto it = Entries.begin(); it != Entries.end(); ++it)
            if (it->second.LastUsedFrame < oldest->second.LastUsedFrame)
                oldest = it;
        if (oldest->second.LastUsedFrame == FrameClock)
            break; // everything left was drawn this frame
        Entries.erase(oldest);
    }
}

void SceneThumbnailCache::Clear()
{
    Entries.clear();
}
