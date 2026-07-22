#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Sphere.h>
#include <render/LightComponentTypes.h>
#include <render/RenderEntityKey.h>
#include <render/RenderLight.h>
#include <render/ShadowAtlasAllocator.h>

#include <cstdint>

struct SpotShadowRequest
{
    RenderEntityKey Key;
    std::uint32_t LightIndex = UINT32_MAX;
    float Score = 0.0f;
    std::uint32_t TileSize = kSpotShadowTileExtent;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    std::uint64_t StateHash = 0;
    Mat4 ViewProjection = Mat4::Identity();
    Vec4 SamplingParams;
    Sphere Bounds;
};

[[nodiscard]] std::uint64_t HashSpotShadowState(
    const SpotShadowView& view, std::uint32_t tileSize);

struct PointShadowRequest
{
    RenderEntityKey Key;
    std::uint32_t LightIndex = UINT32_MAX;
    float Score = 0.0f;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    std::uint64_t StateHash = 0;
    PointShadowView View;
    Sphere Bounds;
};

[[nodiscard]] std::uint64_t HashPointShadowState(const PointShadowView& view);

struct SpotShadowViewJob
{
    std::uint32_t SlotIndex = UINT32_MAX;
    ShadowAtlasAllocation Allocation;
    Mat4 ViewProjection = Mat4::Identity();
};

struct PointShadowFaceJob
{
    std::uint32_t SlotIndex = UINT32_MAX;
    std::uint32_t Face = 0;
    Mat4 ViewProjection = Mat4::Identity();
};

struct SpotShadowGrant
{
    std::uint32_t LightIndex = UINT32_MAX;
    std::uint32_t SlotIndex = UINT32_MAX;
};

struct PointShadowGrant
{
    std::uint32_t LightIndex = UINT32_MAX;
    std::uint32_t SlotIndex = UINT32_MAX;
};

struct ShadowResidencyBudgets
{
    std::uint32_t MaxSlots = kMaxSpotShadows;
    std::uint32_t MaxPointSlots = kMaxPointShadows;
    std::uint32_t MaxViewsPerFrame = 12;
    std::uint32_t MinInvalidatedViewsPerFrame = 1;
};

struct SpotShadowSlotInfo
{
    bool Live = false;
    RenderEntityKey Owner;
    ShadowAtlasAllocation Allocation;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    bool EverRendered = false;
    bool Invalid = false;
    std::uint32_t FramesSinceAcquired = 0;
    std::uint32_t FramesSinceRendered = 0;
};

struct PointShadowSlotInfo
{
    bool Live = false;
    RenderEntityKey Owner;
    ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
    bool EverRendered = false;
    bool Invalid = false;
    std::uint32_t PendingFaces = 0;
    std::uint32_t FramesSinceAcquired = 0;
    std::uint32_t FramesSinceRendered = 0;
};

struct ShadowPoolFrameStats
{
    std::uint32_t RequestCount = 0;
    std::uint32_t HeldRequests = 0;
    std::uint32_t DeniedRequests = 0;
    std::uint32_t CachedSlots = 0;
};

struct ShadowFrameStats
{
    ShadowPoolFrameStats Spot;
    ShadowPoolFrameStats Point;
    std::uint32_t ViewsScheduled = 0;
};
