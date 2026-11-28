#pragma once

#include <ecs/EntityId.h>
#include <math/geometry/3d/ApertureFrame.h>
#include <math/geometry/3d/Transform3d.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneId.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class RuntimeWorld;

// One link while both of its apertures are resident and valid: every consumer
// of a link reads this record and rebuilds none of it.
struct ResolvedLink
{
    LinkId Id{};
    EntityId Link{};
    EntityId ApertureA{};
    EntityId ApertureB{};
    ZoneId ZoneA{};
    ZoneId ZoneB{};
    ApertureFrame FrameA{};
    ApertureFrame FrameB{};
    Transform3f AToB{};
    Transform3f BToA{};
    std::uint32_t Directions = DockDirectionBoth;
    bool Upright = false;
};

enum class LinkResolution : std::uint8_t
{
    // Only in a transition: the link was not, or is no longer, authored.
    Absent,
    DemandOnly,
    AwaitingA,
    AwaitingB,
    AwaitingBoth,
    Active,
    Invalid,
};

enum class LinkFault : std::uint8_t
{
    None,
    OneEndpoint,
    SameAperture,
    ClaimedTwice,
    NotAnAperture,
    ZoneMismatch,
    InvalidFrame,
    ExtentsDiffer,
    Moved,
};

[[nodiscard]] const char* LinkResolutionName(LinkResolution resolution);
[[nodiscard]] const char* LinkFaultName(LinkFault fault);

struct LinkState
{
    LinkId Id{};
    EntityId Link{};
    ZoneId ZoneA{};
    ZoneId ZoneB{};
    LinkResolution Resolution = LinkResolution::DemandOnly;
    LinkFault Fault = LinkFault::None;
};

struct LinkTransition
{
    LinkId Id{};
    LinkResolution From = LinkResolution::Absent;
    LinkResolution To = LinkResolution::Absent;
    LinkFault Fault = LinkFault::None;
};

struct ResolvedLinksStats
{
    std::uint32_t Active = 0;
    std::uint32_t Awaiting = 0;
    std::uint32_t Invalid = 0;
    std::uint32_t DemandOnly = 0;
    // Rebuilds in the last update, which is zero or one, and since creation.
    std::uint32_t Rebuilds = 0;
    std::uint64_t TotalRebuilds = 0;
};

// World resource holding every authored link's resolution and the records of
// the active ones, in link id order. Updated once a frame on the frame thread,
// so the span is the same for every fixed tick and render extraction of that
// frame. docs/zone/links.md, "Resolution".
class ResolvedLinks
{
public:
    ResolvedLinks();
    ~ResolvedLinks();
    ResolvedLinks(const ResolvedLinks&) = delete;
    ResolvedLinks& operator=(const ResolvedLinks&) = delete;

    [[nodiscard]] std::span<const ResolvedLink> Active() const { return Active_; }
    [[nodiscard]] std::span<const LinkState> States() const { return States_; }
    // What changed state in the last update; each change is listed once.
    [[nodiscard]] std::span<const LinkTransition> Transitions() const { return Transitions_; }
    [[nodiscard]] const ResolvedLinksStats& Stats() const { return Stats_; }
    [[nodiscard]] const ResolvedLink* Find(LinkId id) const;

    // Rebuilds only when a link, an aperture, an aperture's transform, a
    // resolved entity's life or a named zone's residency may have changed.
    void Update(RuntimeWorld& runtime);

private:
    struct Cache;

    void Rebuild(RuntimeWorld& runtime);
    [[nodiscard]] bool NeedsRebuild(const RuntimeWorld& runtime);

    std::vector<ResolvedLink> Active_;
    std::vector<LinkState> States_;
    std::vector<LinkTransition> Transitions_;
    ResolvedLinksStats Stats_;
    std::unique_ptr<Cache> Cache_;
};

struct LinkContinuation
{
    const ResolvedLink* Link = nullptr;
    // The aperture the segment entered through the front of.
    DockSide Entered = DockSide::A;
    ApertureCrossing Crossing{};
    // The rest of the segment, carried through the mapping: from where it
    // leaves the other aperture to where its end lands.
    Vec3d Exit = Vec3d::Zero();
    Vec3d End = Vec3d::Zero();
};

// The first aperture a segment passes through from the front, in a direction
// its link allows, and the remainder of the segment carried through. Earliest
// crossing first; a tie goes to the lower link id, then side A. One hop: the
// remainder is not tested again.
[[nodiscard]] std::optional<LinkContinuation> ContinueThroughLinks(
    std::span<const ResolvedLink> links, const Vec3d& from, const Vec3d& to,
    float edgeTolerance = ApertureTolerance::RectangleEdge);
