#include "SceneThumbnailCache.h"

#include <graphics/vulkan/UiTextureResidency.h>
#include <ui/UiService.h>

#include "SceneRenderQueueBuilder.h"
#include "SceneSourceStage.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

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
            if (const std::optional<Aabb3d> brush = scene.EvaluatedWorldBounds(entity))
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

std::string SceneThumbnailCache::ImageName(const std::string& assetPath)
{
    if (Ui == nullptr)
        return {};
    Entry& entry = Entries[assetPath];
    if (!entry.Target.IsValid())
    {
        entry.Target = Studio.AllocateTarget();
        entry.RemainingPasses = ThumbnailStudio::PassesPerTarget();
    }
    std::string name = "scene:" + assetPath;
    // Registered for as long as the process runs: the document engine keeps a
    // texture per source, so a name re-registered under a new id would leave
    // it drawing the old one.
    if (entry.Image == kInvalidUiHostImage)
        entry.Image = Ui->SetHostImage(name, RenderExtent{ 1, 1 });
    entry.LastUsedFrame = FrameClock;
    Studio.Show(entry.Target);
    return name;
}

void SceneThumbnailCache::PublishImages(UiTextureResidency& textures)
{
    for (const UiHostImageId image : Gone)
        textures.WithdrawHostImage(image);
    Gone.clear();
    for (auto& [path, entry] : Entries)
    {
        if (entry.Image == kInvalidUiHostImage || entry.RemainingPasses > 0 || entry.Failed)
            continue;
        if (const std::optional<std::uint32_t> image = Studio.ReadableImage(entry.Target))
            textures.PublishHostImage(entry.Image, BindlessImageIndex{ *image });
    }
}

void SceneThumbnailCache::AppendLiveViewports(std::vector<ViewportId>& live) const
{
    for (const auto& [path, entry] : Entries)
        if (entry.Target.IsValid())
            live.push_back(entry.Target);
}

bool SceneThumbnailCache::LoadEntry(const std::string& assetPath, Entry& entry)
{
    std::string error;
    entry.Stage = SceneSourceStage::Prepare(assetPath, ContentRoots, &Assets, Logging, &error);
    if (entry.Stage == nullptr)
    {
        Logging.GetLogger<SceneThumbnailCache>().Warn("scene thumbnail: could not place '{}': {}", assetPath, error);
        return false;
    }
    entry.Stage->Build();
    entry.Camera = ThumbnailStudio::FrameSubject(
        RenderableBounds(entry.Stage->Document(), Meshes, Assets.SkinnedMeshes.get()));
    return true;
}

void SceneThumbnailCache::RenderPending(const FrameContext& frame)
{
    // Scratch payloads whose recorded passes are safely retired go first: the
    // preview keeps only its target once every slot holds the image.
    for (auto& [path, entry] : Entries)
        if (entry.Stage != nullptr && FrameClock > entry.ReleasePayloadAfter)
            entry.Stage.reset();

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

    const SceneRenderQueueBuilder& built = *pending->Stage->Queues();
    const RenderQueue* queues[] = { &built.BrushQueue(), &built.MeshQueue() };
    if (!Studio.RenderPass(frame, pending->Target, pending->Camera, queues))
        return;

    if (--pending->RemainingPasses == 0)
        pending->ReleasePayloadAfter =
            FrameClock + static_cast<std::uint64_t>(ThumbnailStudio::PassesPerTarget());
}

bool SceneThumbnailCache::HasPendingWork() const
{
    return std::ranges::any_of(Entries, [](const auto& item) {
        const Entry& entry = item.second;
        return entry.Stage != nullptr || (entry.RemainingPasses > 0 && !entry.Failed);
    });
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
        if (oldest->second.Image != kInvalidUiHostImage)
            Gone.push_back(oldest->second.Image);
        Entries.erase(oldest);
    }
}

void SceneThumbnailCache::Invalidate(std::span<const std::string> assetPaths)
{
    if (assetPaths.empty())
        return;
    for (auto& [path, entry] : Entries)
    {
        entry.Stage.reset();
        entry.Loaded = false;
        entry.Failed = false;
        entry.ReleasePayloadAfter = UINT64_MAX;
        entry.RemainingPasses = ThumbnailStudio::PassesPerTarget();
    }
}

void SceneThumbnailCache::Clear()
{
    for (const auto& [path, entry] : Entries)
        if (entry.Image != kInvalidUiHostImage)
            Gone.push_back(entry.Image);
    Entries.clear();
}
