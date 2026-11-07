#pragma once

#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <render/RenderQueue.h>
#include <render/outline/OutlineStyles.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class World;

// The frame's resolved outlines, and the draws that put them in the mask. Empty
// when nothing is highlighted, and then extraction does no lookup per entity.
struct OutlineFrame
{
    std::unordered_map<EntityId, OutlineStyleId, EntityIdHash> StyleOf;
    // One queue per slot, so no field joins RenderQueueItem or run identity.
    std::array<RenderQueue, kMaxOutlineStyles + 1> Queues;
    // Slots that have items, lowest priority first: writes land in primitive order,
    // so the higher priority wins an exact depth tie.
    std::vector<OutlineStyleId> DrawOrder;
    OutlineStyleTable Styles{};
    float MaxWidthPixels = 0.0f;
    // What the queued items cover in normalized device coordinates, x then y, each
    // from -1 to 1. The whole view when an item reaches behind the camera.
    float ScreenMin[2] = { 1.0f, 1.0f };
    float ScreenMax[2] = { -1.0f, -1.0f };
    // Moves when the World's styles change, so the table is uploaded only then.
    std::uint32_t StylesVersion = 0;

    [[nodiscard]] bool IsEmpty() const { return StyleOf.empty(); }
    // The queue an entity's sections also go to, or null when it has no outline.
    [[nodiscard]] RenderQueue* QueueFor(EntityId entity);
    void Reset();
    // Sorts what extraction filled, and works out the draw order and screen bounds.
    void Finish(const Mat4& viewProjection);
    // The screen bounds alone, for a host replaying these queues under another camera.
    void UpdateScreenBounds(const Mat4& viewProjection);
    [[nodiscard]] OutlineCompositeInputs CompositeInputs() const;
};

// Sweeps contributions whose entity or holder has gone, then resolves the rest.
void ExtractOutlines(World& world, OutlineFrame& out);
