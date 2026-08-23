#include <render/ProjectedShadowFramePolicy.h>

#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

std::uint32_t RankAndClampProjectedCasters(ProjectedShadowSet& set,
                                           const Vec<3>& viewOrigin,
                                           std::uint32_t maxCasters)
{
    std::sort(set.Casters.begin(), set.Casters.end(),
              [&](const ProjectedShadowCaster& left, const ProjectedShadowCaster& right)
              {
                  const Vec<3> leftOffset = left.WorldBounds.Center() - viewOrigin;
                  const Vec<3> rightOffset = right.WorldBounds.Center() - viewOrigin;
                  const float leftDistance = leftOffset.Dot(leftOffset);
                  const float rightDistance = rightOffset.Dot(rightOffset);
                  if (leftDistance != rightDistance)
                      return leftDistance < rightDistance;
                  return left.Key < right.Key;
              });

    if (set.Casters.size() <= maxCasters)
        return 0;
    const std::uint32_t dropped =
        static_cast<std::uint32_t>(set.Casters.size()) - maxCasters;
    set.Casters.resize(maxCasters);
    return dropped;
}

ProjectedShadowTileGrid MakeProjectedShadowTileGrid(std::uint32_t maxCasters,
                                                    std::uint32_t tilePixels)
{
    ProjectedShadowTileGrid grid;
    grid.TilePixels = tilePixels;
    grid.TilesPerRow = 1;
    while (grid.TilesPerRow * grid.TilesPerRow < maxCasters)
        ++grid.TilesPerRow;
    grid.AtlasExtent = grid.TilesPerRow * tilePixels;
    return grid;
}

ProjectedShadowTileRect ProjectedShadowTileRectFor(const ProjectedShadowTileGrid& grid,
                                                   std::uint32_t index)
{
    return ProjectedShadowTileRect{
        .X = static_cast<std::int32_t>((index % grid.TilesPerRow) * grid.TilePixels),
        .Y = static_cast<std::int32_t>((index / grid.TilesPerRow) * grid.TilePixels),
        .Extent = grid.TilePixels,
    };
}

Vec4 ProjectedShadowTileUvScaleBias(const ProjectedShadowTileGrid& grid,
                                    std::uint32_t index)
{
    const float scale = 1.0f / static_cast<float>(grid.TilesPerRow);
    return Vec4(scale, scale,
                static_cast<float>(index % grid.TilesPerRow) * scale,
                static_cast<float>(index / grid.TilesPerRow) * scale);
}

ProjectedShadowProjectionFit FitProjectedShadowProjection(
    const ProjectedShadowCaster& caster, float maxDistance)
{
    const Vec<3> center = caster.WorldBounds.Center();
    const Vec<3> direction = caster.Direction;

    // A stable basis around the shadow direction: world up unless the shadow
    // is near-vertical, then world X. The choice only has to be continuous in
    // the direction, which the |Y| test is everywhere the blend can go.
    const Vec<3> upHint = std::abs(direction.Y) < 0.99f
        ? Vec<3>(0.0f, 1.0f, 0.0f)
        : Vec<3>(1.0f, 0.0f, 0.0f);

    // Eye behind the caster along the direction, far enough that every corner
    // lands in front of the view.
    const Vec<3> halfExtent = caster.WorldBounds.HalfExtent();
    const float radius = std::sqrt(halfExtent.Dot(halfExtent));
    const Vec<3> eye = center - direction * (radius + 1.0f);
    const Mat4 view = Mat4::MakeLookAt(eye, center, upHint);

    // Fit the ortho box to the bounds corners in light space, padded a few
    // percent so the silhouette never reaches the tile border.
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    float minZ = 0.0f, maxZ = 0.0f;
    bool first = true;
    const Vec<3>& lo = caster.WorldBounds.Min;
    const Vec<3>& hi = caster.WorldBounds.Max;
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec4 world((corner & 1) != 0 ? hi.X : lo.X,
                         (corner & 2) != 0 ? hi.Y : lo.Y,
                         (corner & 4) != 0 ? hi.Z : lo.Z, 1.0f);
        const Vec4 light = view * world;
        if (first)
        {
            minX = maxX = light.X;
            minY = maxY = light.Y;
            minZ = maxZ = light.Z;
            first = false;
        }
        else
        {
            minX = std::min(minX, light.X); maxX = std::max(maxX, light.X);
            minY = std::min(minY, light.Y); maxY = std::max(maxY, light.Y);
            minZ = std::min(minZ, light.Z); maxZ = std::max(maxZ, light.Z);
        }
    }

    const float pad = 0.05f * std::max(maxX - minX, maxY - minY) + 1e-3f;
    // Look-at spaces put "in front" at negative Z: the near distance is the
    // corner closest to the eye, the far distance the deepest corner plus the
    // projection reach.
    const float nearPlane = std::max(-maxZ - pad, 1e-3f);
    const float farPlane = -minZ + maxDistance + pad;
    return ProjectedShadowProjectionFit{
        .ViewProjection = MakeVulkanOrthographic(minX - pad, maxX + pad,
                                                 minY - pad, maxY + pad,
                                                 nearPlane, farPlane) * view,
        .DepthRange = farPlane - nearPlane,
    };
}

Aabb3d ProjectedShadowSweptBounds(const ProjectedShadowCaster& caster,
                                  float maxDistance)
{
    Aabb3d swept = caster.WorldBounds;
    Aabb3d shifted = caster.WorldBounds;
    const Vec<3> offset = caster.Direction * maxDistance;
    shifted.Min = shifted.Min + offset;
    shifted.Max = shifted.Max + offset;
    swept.ExpandToInclude(shifted);
    return swept;
}

ProjectedShadowScreenRect ComputeProjectedShadowScreenRect(
    const Aabb3d& sweptBounds,
    const Mat4& cameraViewProjection,
    std::uint32_t targetWidth,
    std::uint32_t targetHeight)
{
    float minX = 1.0f, minY = 1.0f, maxX = -1.0f, maxY = -1.0f;
    int behind = 0;
    const Vec<3>& lo = sweptBounds.Min;
    const Vec<3>& hi = sweptBounds.Max;
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec4 clip = cameraViewProjection
            * Vec4((corner & 1) != 0 ? hi.X : lo.X,
                   (corner & 2) != 0 ? hi.Y : lo.Y,
                   (corner & 4) != 0 ? hi.Z : lo.Z, 1.0f);
        if (clip.W <= 1e-5f)
        {
            // A corner behind the eye has no meaningful projection.
            ++behind;
            continue;
        }
        const float x = clip.X / clip.W;
        const float y = clip.Y / clip.W;
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }

    // Entirely behind the eye: nothing to shade. Straddling the near plane: a
    // rect built from the surviving corners could clip visible shadow, so be
    // conservative -- a larger rect costs fill, a smaller one clips.
    if (behind == 8)
        return ProjectedShadowScreenRect{};
    if (behind > 0)
        return ProjectedShadowScreenRect{ 0, 0, targetWidth, targetHeight };
    if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f)
        return ProjectedShadowScreenRect{}; // fully off screen

    const float width = static_cast<float>(targetWidth);
    const float height = static_cast<float>(targetHeight);
    const float left = std::clamp((minX * 0.5f + 0.5f) * width, 0.0f, width);
    const float right = std::clamp((maxX * 0.5f + 0.5f) * width, 0.0f, width);
    const float top = std::clamp((minY * 0.5f + 0.5f) * height, 0.0f, height);
    const float bottom = std::clamp((maxY * 0.5f + 0.5f) * height, 0.0f, height);

    ProjectedShadowScreenRect rect;
    rect.X = static_cast<std::int32_t>(left);
    rect.Y = static_cast<std::int32_t>(top);
    rect.Width = static_cast<std::uint32_t>(std::ceil(right) - std::floor(left));
    rect.Height = static_cast<std::uint32_t>(std::ceil(bottom) - std::floor(top));
    return rect;
}

std::uint32_t GatherProjectedShadowReceivers(std::span<const RenderQueueItem> items,
                                             const Aabb3d& sweptBounds,
                                             std::uint32_t cap,
                                             std::vector<std::uint32_t>& outIndices)
{
    std::uint32_t excluded = 0;
    for (std::uint32_t index = 0; index < items.size(); ++index)
    {
        const RenderQueueItem& item = items[index];
        if (item.SkinnedMesh.IsValid())
            continue;
        if (!sweptBounds.Intersects(item.WorldBounds))
            continue;
        if (outIndices.size() >= cap)
        {
            ++excluded;
            continue;
        }
        outIndices.push_back(index);
    }
    return excluded;
}

ProjectedShadowScreenRect UnionProjectedShadowScreenRects(
    std::span<const ProjectedShadowScreenRect> rects)
{
    std::int32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    for (const ProjectedShadowScreenRect& rect : rects)
    {
        if (rect.Width == 0 || rect.Height == 0)
            continue;
        const std::int32_t right = rect.X + static_cast<std::int32_t>(rect.Width);
        const std::int32_t bottom = rect.Y + static_cast<std::int32_t>(rect.Height);
        if (!any)
        {
            minX = rect.X; minY = rect.Y; maxX = right; maxY = bottom;
            any = true;
        }
        else
        {
            minX = std::min(minX, rect.X);
            minY = std::min(minY, rect.Y);
            maxX = std::max(maxX, right);
            maxY = std::max(maxY, bottom);
        }
    }
    if (!any)
        return {};
    return ProjectedShadowScreenRect{
        .X = minX,
        .Y = minY,
        .Width = static_cast<std::uint32_t>(maxX - minX),
        .Height = static_cast<std::uint32_t>(maxY - minY),
    };
}
