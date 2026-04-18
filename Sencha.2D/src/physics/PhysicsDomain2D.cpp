#include <physics/PhysicsDomain2D.h>
#include <physics/NarrowPhase2D.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PhysicsDomain2D::PhysicsDomain2D(const PhysicsConfig2D& config)
    : Tree(QuadTree<uint32_t>::Config{
          config.TreeBounds,
          config.TreeMaxDepth,
          config.TreeMaxEntriesPerLeaf })
{
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

bool PhysicsDomain2D::Register(EntityId entity, const Collider2D& collider)
{
    if (!entity.IsValid())
        return false;

    Colliders.Emplace(entity.Index, collider);
    return true;
}

bool PhysicsDomain2D::Unregister(EntityId entity)
{
    return entity.IsValid() && Colliders.Remove(entity.Index);
}

void PhysicsDomain2D::UpdateBounds(EntityId entity, const Aabb2d& worldBounds)
{
    Collider2D* collider = entity.IsValid() ? Colliders.TryGet(entity.Index) : nullptr;
    if (collider)
        collider->WorldBounds = worldBounds;
}

bool PhysicsDomain2D::Contains(EntityId entity) const
{
    return entity.IsValid() && Colliders.Contains(entity.Index);
}

// ---------------------------------------------------------------------------
// Broadphase rebuild
// ---------------------------------------------------------------------------

void PhysicsDomain2D::RebuildTree()
{
    Tree.Clear();

    const std::vector<Collider2D>& colliders = Colliders.GetItems();
    for (uint32_t i = 0; i < static_cast<uint32_t>(colliders.size()); ++i)
    {
        const Collider2D& collider = colliders[i];
        if (!collider.WorldBounds.IsValid()) continue;
        Tree.Insert(i, collider.WorldBounds);
    }
}

// ---------------------------------------------------------------------------
// Spatial queries
// ---------------------------------------------------------------------------

void PhysicsDomain2D::OverlapBox(const Aabb2d& box,
                                 std::vector<EntityId>& out) const
{
    out.clear();

    ScratchCandidates.clear();
    GatherCandidates(box, ScratchCandidates);
    const auto& candidates = ScratchCandidates;
    const std::vector<Collider2D>& colliders = Colliders.GetItems();
    const std::vector<Id>& owners = Colliders.GetOwners();

    for (uint32_t idx : candidates)
    {
        if (idx >= colliders.size()) continue;
        const Collider2D& collider = colliders[idx];
        if (!collider.WorldBounds.Intersects(box)) continue;

        // Deduplicate (grid cells can reference the same entry multiple times)
        const EntityId entity{ owners[idx], 0 };
        bool duplicate = false;
        for (const EntityId& h : out)
        {
            if (h == entity) { duplicate = true; break; }
        }
        if (!duplicate)
            out.push_back(entity);
    }
}

PhysicsDomain2D::SweepHit PhysicsDomain2D::SweepBox(const Aabb2d& box,
                                                     Vec2d delta) const
{
    SweepHit best;

    const float invDX = (std::abs(delta.X) > 1e-6f)
                            ? 1.0f / static_cast<float>(delta.X)
                            : std::numeric_limits<float>::max();
    const float invDY = (std::abs(delta.Y) > 1e-6f)
                            ? 1.0f / static_cast<float>(delta.Y)
                            : std::numeric_limits<float>::max();

    // Broadphase: AABB of the full swept region
    Aabb2d sweptBounds = Aabb2d::FromMinMax(
        Vec2d{ std::min(box.Min.X, box.Min.X + delta.X),
               std::min(box.Min.Y, box.Min.Y + delta.Y) },
        Vec2d{ std::max(box.Max.X, box.Max.X + delta.X),
               std::max(box.Max.Y, box.Max.Y + delta.Y) });

    ScratchCandidates.clear();
    GatherCandidates(sweptBounds, ScratchCandidates);
    const auto& candidates = ScratchCandidates;
    const std::vector<Collider2D>& colliders = Colliders.GetItems();
    const std::vector<Id>& owners = Colliders.GetOwners();

    Vec2d halfExt = box.HalfExtent();

    for (uint32_t idx : candidates)
    {
        if (idx >= colliders.size()) continue;
        const Collider2D& collider = colliders[idx];
        if (!collider.WorldBounds.IsValid()) continue;

        // Minkowski-expand the static collider by the mover's half-extents
        const Aabb2d& s = collider.WorldBounds;
        Aabb2d expanded = Aabb2d::FromMinMax(
            Vec2d{ s.Min.X - halfExt.X, s.Min.Y - halfExt.Y },
            Vec2d{ s.Max.X + halfExt.X, s.Max.Y + halfExt.Y });

        Vec2d origin = box.Center();

        // Slab method — entry/exit times per axis
        float txEntry = (delta.X != 0.0)
            ? static_cast<float>((expanded.Min.X - origin.X)) * invDX
            : -std::numeric_limits<float>::max();
        float txExit  = (delta.X != 0.0)
            ? static_cast<float>((expanded.Max.X - origin.X)) * invDX
            :  std::numeric_limits<float>::max();
        float tyEntry = (delta.Y != 0.0)
            ? static_cast<float>((expanded.Min.Y - origin.Y)) * invDY
            : -std::numeric_limits<float>::max();
        float tyExit  = (delta.Y != 0.0)
            ? static_cast<float>((expanded.Max.Y - origin.Y)) * invDY
            :  std::numeric_limits<float>::max();

        if (txEntry > txExit) std::swap(txEntry, txExit);
        if (tyEntry > tyExit) std::swap(tyEntry, tyExit);

        float tEntry = std::max(txEntry, tyEntry);
        float tExit  = std::min(txExit,  tyExit);

        if (tEntry > tExit || tExit < 0.0f || tEntry > 1.0f) continue;

        float t = std::max(0.0f, tEntry);
        if (t < best.Time)
        {
            best.Time   = t;
            best.Entity = EntityId{ owners[idx], 0 };
            best.DidHit = true;
        }
    }

    return best;
}

void PhysicsDomain2D::SetCollisionGrid(const CollisionGrid2D* grid)
{
    CollGrid = grid;
}

// ---------------------------------------------------------------------------
// MoveProjected — iterative velocity-projection resolver for circles
// ---------------------------------------------------------------------------

MoveResult2D PhysicsDomain2D::MoveProjected(Vec2d center, float radius,
                                             Vec2d delta,
                                             EntityId exclude) const
{
    constexpr int   MaxIterations = 4;
    constexpr float kEpsSq        = 1e-8f;  // stop when remaining speed² < this
    constexpr float kTOIEpsilon   = 1e-4f;  // simultaneous-contact window
    constexpr float kPullback     = 1e-4f;  // back off from surface to avoid sticking

    Vec2d      pos      = center;
    Vec2d      velocity = delta;
    HitFlags2D hits     = HitFlags2D::None;

    std::vector<DomainContact> contacts;
    contacts.reserve(16);

    for (int iter = 0; iter < MaxIterations; ++iter)
    {
        if (velocity.SqrMagnitude() < kEpsSq) break;

        contacts.clear();
        if (CollGrid) GatherGridContacts(pos, radius, velocity, contacts);
        GatherDomainContacts(pos, radius, velocity, exclude, contacts);

        // Find earliest TOI
        float bestTOI = 2.0f;
        for (const auto& c : contacts)
            if (c.Base.TOI < bestTOI) bestTOI = c.Base.TOI;

        if (bestTOI > 1.0f)
        {
            // Free move — consume remaining velocity and stop iterating
            pos.X += velocity.X;
            pos.Y += velocity.Y;
            break;
        }

        // Advance to the contact, pulling back slightly to stay off the surface
        float safeT = std::max(0.0f, bestTOI - kPullback);
        pos.X += velocity.X * safeT;
        pos.Y += velocity.Y * safeT;

        // Remaining velocity after the safe advance
        float remainScale = 1.0f - safeT;
        Vec2d remaining   = { velocity.X * remainScale, velocity.Y * remainScale };

        // Project remaining velocity against every contact simultaneous with
        // the earliest. Multiple simultaneous contacts handle wedge corners.
        for (const auto& c : contacts)
        {
            if (c.Base.TOI > bestTOI + kTOIEpsilon) continue;

            float d = remaining.X * c.Base.Normal.X + remaining.Y * c.Base.Normal.Y;
            if (d < 0.0f)
            {
                // Remove the component pushing into this surface
                remaining.X -= d * c.Base.Normal.X;
                remaining.Y -= d * c.Base.Normal.Y;
            }

            // Accumulate hit flags based on surface orientation
            if      (c.Base.Normal.Y >  0.5f) hits |= HitFlags2D::Floor;
            else if (c.Base.Normal.Y < -0.5f) hits |= HitFlags2D::Ceiling;
            else                               hits |= HitFlags2D::Wall;
        }

        // Crease detection: if projection reversed the velocity (acute corner),
        // the circle is wedged — stop rather than jitter
        float creaseCheck = remaining.X * velocity.X + remaining.Y * velocity.Y;
        if (creaseCheck < 0.0f) break;

        velocity = remaining;
    }

    MoveResult2D result;
    result.ResolvedDelta = { pos.X - center.X, pos.Y - center.Y };
    result.Hits          = hits;
    return result;
}

// ---------------------------------------------------------------------------
// GatherGridContacts
// ---------------------------------------------------------------------------

void PhysicsDomain2D::GatherGridContacts(Vec2d center, float radius,
                                          Vec2d velocity,
                                          std::vector<DomainContact>& out) const
{
    Aabb2d swept = Aabb2d::FromMinMax(
        Vec2d{ std::min(center.X, center.X + velocity.X) - radius,
               std::min(center.Y, center.Y + velocity.Y) - radius },
        Vec2d{ std::max(center.X, center.X + velocity.X) + radius,
               std::max(center.Y, center.Y + velocity.Y) + radius });

    CollGrid->ForEachSolidInRegion(swept, [&](int col, int row, const Aabb2d& bounds)
    {
        CircleContact base = SweepCircleVsAabb(center, radius, velocity, bounds);
        if (base.TOI > 1.0f) return;
        if (IsGhostEdge(base.Normal, *CollGrid, col, row)) return;

        out.push_back({ base, col, row });
    });
}

// ---------------------------------------------------------------------------
// GatherDomainContacts
// ---------------------------------------------------------------------------

void PhysicsDomain2D::GatherDomainContacts(Vec2d center, float radius,
                                            Vec2d velocity,
                                            EntityId exclude,
                                            std::vector<DomainContact>& out) const
{
    Aabb2d swept = Aabb2d::FromMinMax(
        Vec2d{ std::min(center.X, center.X + velocity.X) - radius,
               std::min(center.Y, center.Y + velocity.Y) - radius },
        Vec2d{ std::max(center.X, center.X + velocity.X) + radius,
               std::max(center.Y, center.Y + velocity.Y) + radius });

    ScratchCandidates.clear();
    GatherCandidates(swept, ScratchCandidates);
    const std::vector<Collider2D>& colliders = Colliders.GetItems();
    const std::vector<Id>& owners = Colliders.GetOwners();

    for (uint32_t idx : ScratchCandidates)
    {
        if (idx >= colliders.size()) continue;
        if (exclude.IsValid() && owners[idx] == exclude.Index) continue;

        const Collider2D& collider = colliders[idx];
        if (!collider.WorldBounds.IsValid()) continue;

        CircleContact base = SweepCircleVsAabb(
            center, radius, velocity, collider.WorldBounds);
        if (base.TOI > 1.0f) continue;

        // GridCol/GridRow = -1: no ghost-edge suppression for dynamic objects
        out.push_back({ base, -1, -1 });
    }
}

MoveResult2D PhysicsDomain2D::MoveBox(const Aabb2d& box, Vec2d desiredDelta,
                                       EntityId exclude) const
{
    MoveResult2D result;

    // Resolve X first
    bool hitRight = false, hitLeft = false;
    float safeX = ResolveAxis(box, static_cast<float>(desiredDelta.X),
                              /*isVertical=*/false, hitRight, hitLeft, exclude);

    // Shift box by safe X before resolving Y
    Aabb2d boxAfterX = Aabb2d::FromCenterHalfExtent(
        Vec2d{ box.Center().X + safeX, box.Center().Y },
        box.HalfExtent());

    // Resolve Y
    bool hitUp = false, hitDown = false;
    float safeY = ResolveAxis(boxAfterX, static_cast<float>(desiredDelta.Y),
                              /*isVertical=*/true, hitUp, hitDown, exclude);

    result.ResolvedDelta = { safeX, safeY };
    if (hitDown)              result.Hits |= HitFlags2D::Floor;
    if (hitUp)                result.Hits |= HitFlags2D::Ceiling;
    if (hitLeft || hitRight)  result.Hits |= HitFlags2D::Wall;

    return result;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void PhysicsDomain2D::GatherCandidates(const Aabb2d& box,
                                       std::vector<uint32_t>& out) const
{
    Tree.Query(box, std::back_inserter(out));
}

float PhysicsDomain2D::ResolveAxis(const Aabb2d& box, float delta,
                                   bool isVertical,
                                   bool& hitPositive, bool& hitNegative,
                                   EntityId exclude) const
{
    if (std::abs(delta) < 1e-6f) return 0.0f;

    // Broadphase swept AABB for this axis
    Aabb2d sweptBox;
    if (isVertical)
    {
        sweptBox = Aabb2d::FromMinMax(
            Vec2d{ box.Min.X, std::min(box.Min.Y, box.Min.Y + delta) },
            Vec2d{ box.Max.X, std::max(box.Max.Y, box.Max.Y + delta) });
    }
    else
    {
        sweptBox = Aabb2d::FromMinMax(
            Vec2d{ std::min(box.Min.X, box.Min.X + delta), box.Min.Y },
            Vec2d{ std::max(box.Max.X, box.Max.X + delta), box.Max.Y });
    }

    ScratchCandidates.clear();
    GatherCandidates(sweptBox, ScratchCandidates);
    const auto& candidates = ScratchCandidates;
    const std::vector<Collider2D>& colliders = Colliders.GetItems();
    const std::vector<Id>& owners = Colliders.GetOwners();

    float safe = delta;

    for (uint32_t idx : candidates)
    {
        if (idx >= colliders.size()) continue;
        if (exclude.IsValid() && owners[idx] == exclude.Index) continue;

        const Collider2D& collider = colliders[idx];
        if (!collider.WorldBounds.IsValid()) continue;

        const Aabb2d& s = collider.WorldBounds;

        // Overlap on the perpendicular axis — only consider colliders in the path
        if (isVertical)
        {
            if (box.Max.X <= s.Min.X || box.Min.X >= s.Max.X) continue;
        }
        else
        {
            if (box.Max.Y <= s.Min.Y || box.Min.Y >= s.Max.Y) continue;
        }

        if (isVertical)
        {
            if (delta > 0.0f)
            {
                // Moving up (+Y): gap between mover top and blocker bottom
                float gap = static_cast<float>(s.Min.Y) - static_cast<float>(box.Max.Y);
                if (gap < safe)
                {
                    safe = std::max(0.0f, gap);
                    hitPositive = true;
                }
            }
            else
            {
                // Moving down (-Y): gap between mover bottom and blocker top
                float gap = static_cast<float>(s.Max.Y) - static_cast<float>(box.Min.Y);
                if (gap > safe)
                {
                    safe = std::min(0.0f, gap);
                    hitNegative = true;
                }
            }
        }
        else
        {
            if (delta > 0.0f)
            {
                // Moving right (+X)
                float gap = static_cast<float>(s.Min.X) - static_cast<float>(box.Max.X);
                if (gap < safe)
                {
                    safe = std::max(0.0f, gap);
                    hitPositive = true;
                }
            }
            else
            {
                // Moving left (-X)
                float gap = static_cast<float>(s.Max.X) - static_cast<float>(box.Min.X);
                if (gap > safe)
                {
                    safe = std::min(0.0f, gap);
                    hitNegative = true;
                }
            }
        }
    }

    return safe;
}
