#include "SceneThumbnailCache.h"

#include "SceneRenderQueueBuilder.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/logging/Logger.h>
#include <math/geometry/3d/AabbTransform.h>
#include <render/CameraProjection.h>
#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <render/StaticMeshComponent.h>
#include <render/pass/MeshForwardPass.h>
#include <graphics/vulkan/SkyGradientPass.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr VkExtent2D kThumbnailExtent{ 256, 256 };
    // One render per in-flight target slot; the highest slot count in use.
    constexpr int kPassesPerThumbnail = 3;
    // Distinct from every layout-minted id: the layout counts up from one,
    // thumbnails count down from the top.
    constexpr std::uint32_t kThumbnailIdBase = 0x40000000;

    // The scene's bounds as the thumbnail should frame them: brush bodies by
    // their world bounds, placed meshes by their transformed local bounds,
    // anything else by its position so an empty-looking scene still frames.
    [[nodiscard]] Aabb3d FrameBounds(const EditorDocument& document,
                                     StaticMeshCache& meshes)
    {
        const EditorScene& scene = document.GetScene();
        Aabb3d bounds = Aabb3d::Empty();
        for (EntityId entity : scene.GetAllEntities())
        {
            if (const std::optional<Aabb3d> brush = scene.TryGetWorldBounds(entity))
            {
                bounds.ExpandToInclude(*brush);
                continue;
            }
            const Transform3f* world = scene.TryGetWorldTransform(entity);
            if (world == nullptr)
                continue;
            const auto* placed = document.GetRegistry()
                                     .Components.TryGet<StaticMeshComponent>(entity);
            const GpuStaticMesh* mesh =
                placed != nullptr ? meshes.Get(placed->Mesh) : nullptr;
            if (mesh != nullptr)
                bounds.ExpandToInclude(
                    TransformAabb(mesh->LocalBounds, world->ToMat4()));
            else
                bounds.ExpandToInclude(Aabb3d::FromMinMax(
                    world->Position - Vec3d::One() * 0.25f,
                    world->Position + Vec3d::One() * 0.25f));
        }
        if (!bounds.IsValid())
            bounds = Aabb3d::FromMinMax(Vec3d::One() * -0.5f, Vec3d::One() * 0.5f);
        return bounds;
    }

    // Three-quarter framing: above, off both axes, looking at the center.
    // The distance is a tight fit of the box's projected corners against the
    // frustum, not a bounding-sphere guess: every scene fills the same
    // fraction of the cell whatever its shape, so a flat floor slab and a
    // tall prop read at the same visual weight.
    [[nodiscard]] CameraRenderData FrameCamera(const Aabb3d& bounds)
    {
        constexpr float kFovY = 35.0f * 3.14159265f / 180.0f;
        const float tanHalf = std::tan(kFovY * 0.5f);
        const Vec3d center = bounds.Center();
        const Vec3d half = (bounds.Max - bounds.Min) * 0.5f;

        const Vec3d direction = Vec3d(1.0f, 0.75f, 1.0f).Normalized();
        const Vec3d forward = direction * -1.0f; // eye looks back at the center
        const Vec3d right = forward.Cross(Vec3d::Up()).Normalized();
        const Vec3d up = right.Cross(forward).Normalized();

        // For each corner: how far back the eye must sit for that corner to
        // stay inside the square frustum, accounting for its own depth.
        float distance = 0.5f;
        for (int i = 0; i < 8; ++i)
        {
            const Vec3d corner((i & 1) != 0 ? half.X : -half.X,
                               (i & 2) != 0 ? half.Y : -half.Y,
                               (i & 4) != 0 ? half.Z : -half.Z);
            const float depth = corner.Dot(forward);
            const float lateral = std::max(std::abs(corner.Dot(right)),
                                           std::abs(corner.Dot(up)));
            distance = std::max(distance, lateral / tanHalf - depth);
        }
        distance *= 1.08f; // breathing room at the cell edge

        const Vec3d eye = center + direction * distance;
        const float radius = std::max(
            0.25f, std::sqrt(half.X * half.X + half.Y * half.Y + half.Z * half.Z));

        CameraRenderData camera;
        camera.Position = eye;
        camera.View = Mat4::MakeLookAt(eye, center, Vec3d::Up());
        camera.Projection = MakeVulkanPerspective(
            kFovY, 1.0f, std::max(0.01f, (distance - radius) * 0.5f),
            distance + radius * 4.0f);
        camera.ViewProjection = camera.Projection * camera.View;
        return camera;
    }
} // namespace

SceneThumbnailCache::SceneThumbnailCache(ViewportTargetCache& targets,
                                         MeshForwardPass& forward,
                                         SkyGradientPass& sky,
                                         RuntimeAssets& assets,
                                         StaticMeshCache& meshes,
                                         MaterialCache& materials,
                                         LoggingProvider& logging,
                                         VkFormat depthFormat)
    : Targets(targets)
    , Forward(forward)
    , Sky(sky)
    , Assets(assets)
    , Meshes(meshes)
    , Materials(materials)
    , Logging(logging)
    , DepthFormat(depthFormat)
{
}

SceneThumbnailCache::~SceneThumbnailCache() = default;

void SceneThumbnailCache::SetContentRoots(std::vector<std::filesystem::path> roots)
{
    ContentRoots = std::move(roots);
}

ImTextureID SceneThumbnailCache::Thumbnail(const std::string& assetPath)
{
    ++FrameClock;
    Entry& entry = Entries[assetPath];
    if (!entry.Target.IsValid())
    {
        entry.Target = ViewportId{ kThumbnailIdBase + NextTargetId++ };
        entry.RemainingPasses = kPassesPerThumbnail;
    }
    entry.LastUsedFrame = FrameClock;
    // Registers the size and creates the target on first sight; returns the
    // current slot's texture, 0 until a render has filled it.
    return Targets.Display(entry.Target, kThumbnailExtent);
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

    constexpr std::string_view prefix = "asset://";
    std::filesystem::path file;
    for (const std::filesystem::path& root : ContentRoots)
    {
        std::error_code ec;
        if (std::filesystem::path candidate =
                root / assetPath.substr(prefix.size());
            std::filesystem::is_regular_file(candidate, ec))
        {
            file = std::move(candidate);
            break;
        }
    }
    if (file.empty() || !entry.Document->Load(file.generic_string()))
    {
        Logging.GetLogger<SceneThumbnailCache>().Warn(
            "scene thumbnail: could not load '{}'", assetPath);
        return false;
    }

    entry.Document->GetScene().RefreshDerivedTransforms();
    entry.Camera = FrameCamera(FrameBounds(*entry.Document, Meshes));

    entry.Queues = std::make_unique<SceneRenderQueueBuilder>(
        Assets.Assets, *Assets.StaticMeshes, Assets.Materials, Assets.MaterialSets,
        Logging, nullptr, Assets.SkinnedMeshes.get(), &Assets.AnimationClips);
    entry.Queues->Build(*entry.Document);
    return true;
}

void SceneThumbnailCache::RenderPending(const FrameContext& frame)
{
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

    const std::optional<ViewportTargetCache::RenderView> target =
        Targets.AcquireForRender(pending->Target);
    if (!target.has_value())
        return;

    RenderTargetSession session(frame.Cmd, target->ColorImage, target->ColorLayout,
                                target->DepthImage);
    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = target->Extent;
    scope.Color.View = target->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };
    scope.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    scope.Depth.View = target->DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    scope.DepthFormat = DepthFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);
        const FrameContext& local = rendering.Context();

        // Studio look: sky gradient behind, full-bright neutral ambient over
        // the geometry -- readable materials with no dependence on any scene's
        // lights or shadows.
        RenderLightSet lights;
        lights.AmbientSky = Vec<3>(1.0f, 1.0f, 1.0f);
        lights.AmbientGround = Vec<3>(0.75f, 0.75f, 0.75f);
        Sky.Draw(local,
                 MakeInverseSkyViewProjection(pending->Camera.View,
                                              pending->Camera.Projection),
                 SkyGradientParams{ .Top = Vec<3>(0.16f, 0.22f, 0.28f),
                                    .Bottom = Vec<3>(0.07f, 0.09f, 0.11f),
                                    .Exposure = 1.0f,
                                    .TonemapKnee = 1.0f,
                                    .TonemapEnabled = false });
        Forward.Draw(local, MeshForwardPass::DrawContext{
                                .Camera = pending->Camera,
                                .Lights = lights,
                                .Queue = pending->Queues->BrushQueue(),
                                .Meshes = Meshes,
                                .Materials = Materials,
                                .SkinnedMeshes = Assets.SkinnedMeshes.get() });
        Forward.Draw(local, MeshForwardPass::DrawContext{
                                .Camera = pending->Camera,
                                .Lights = lights,
                                .Queue = pending->Queues->MeshQueue(),
                                .Meshes = Meshes,
                                .Materials = Materials,
                                .SkinnedMeshes = Assets.SkinnedMeshes.get() });
    }
    session.End();

    --pending->RemainingPasses;
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
