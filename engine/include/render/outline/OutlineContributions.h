#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <presentation/PresentationContributions.h>
#include <presentation/PresentationOwner.h>
#include <render/RenderQueue.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class World;

// A slot in the outline mask, 1 to 15; 0 is no outline. Docs: docs/renderer/outlines.md.
struct OutlineStyleId
{
    std::uint8_t Slot = 0;

    [[nodiscard]] bool IsValid() const { return Slot != 0; }
    friend bool operator==(OutlineStyleId, OutlineStyleId) = default;
};

inline constexpr std::size_t kMaxOutlineStyles = 15;

struct OutlineStyleDesc
{
    // sRGB, as a colour is authored; converted to linear once at registration.
    Vec<3> Color;
    float WidthPixels = 2.0f;
    // Where two styles compete for a pixel, the higher wins. Unique per World.
    std::int16_t Priority = 0;
};

// Null for a sixteenth style or a priority already taken. Styles are per World, so
// a game owns its own and the editor owns its own.
[[nodiscard]] OutlineStyleId RegisterOutlineStyle(World& world, std::string_view name, const OutlineStyleDesc& desc);

struct OutlineContribution
{
    PresentationOwner Owner;
    OutlineStyleId Style;
    // The entity's one style is the highest Order, then the owner that precedes.
    std::int16_t Order = 0;
};

void SetOutline(World& world, EntityId entity, const OutlineContribution& contribution);
void ClearOutline(World& world, EntityId entity, const PresentationOwner& owner);
void ClearOutlinesOf(World& world, PresentationWriterId writer);

// A style as the composite reads it.
struct OutlineStyleEntry
{
    Vec<3> LinearColor;
    float WidthPixels = 0.0f;
    std::int16_t Priority = 0;
};

using OutlineStyleTable = std::array<OutlineStyleEntry, kMaxOutlineStyles + 1>;

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
    // Moves when the World's styles change, so the table is uploaded only then.
    std::uint32_t StylesVersion = 0;

    [[nodiscard]] bool IsEmpty() const { return StyleOf.empty(); }
    // The queue an entity's sections also go to, or null when it has no outline.
    [[nodiscard]] RenderQueue* QueueFor(EntityId entity);
    void Reset();
    // Sorts what extraction filled and works out the draw order.
    void Finish();
};

// Sweeps contributions whose entity or holder has gone, then resolves the rest.
void ExtractOutlines(World& world, OutlineFrame& out);
