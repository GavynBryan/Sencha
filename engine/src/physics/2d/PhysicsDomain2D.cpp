#include <physics/2d/PhysicsDomain2D.h>

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

PhysicsHandle2D PhysicsDomain2D::Register(const Collider2D& collider)
{
    PhysicsHandle2D handle{ NextHandle++ };

    if (!FreeSlots.empty())
    {
        uint32_t slot = FreeSlots.back();
        FreeSlots.pop_back();
        Entries[slot] = { collider, handle, true };
    }
    else
    {
        Entries.push_back({ collider, handle, true });
    }

    return handle;
}

void PhysicsDomain2D::Unregister(PhysicsHandle2D handle)
{
    if (!handle.IsValid()) return;

    for (uint32_t i = 0; i < static_cast<uint32_t>(Entries.size()); ++i)
    {
        if (Entries[i].Live && Entries[i].Handle == handle)
        {
            Entries[i].Live = false;
            FreeSlots.push_back(i);
            return;
        }
    }
}

void PhysicsDomain2D::UpdateBounds(PhysicsHandle2D handle, const Aabb2d& worldBounds)
{
    if (!handle.IsValid()) return;

    for (ColliderEntry& entry : Entries)
    {
        if (entry.Live && entry.Handle == handle)
        {
            entry.Shape.WorldBounds = worldBounds;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Broadphase rebuild
// ---------------------------------------------------------------------------

void PhysicsDomain2D::RebuildTree()
{
    Tree.Clear();

    for (uint32_t i = 0; i < static_cast<uint32_t>(Entries.size()); ++i)
    {
        const ColliderEntry& entry = Entries[i];
        if (!entry.Live || !entry.Shape.WorldBounds.IsValid()) continue;
        Tree.Insert(i, entry.Shape.WorldBounds);
    }
}

// ---------------------------------------------------------------------------
// Spatial queries
// ---------------------------------------------------------------------------

void PhysicsDomain2D::OverlapBox(const Aabb2d& box,
                                 std::vector<PhysicsHandle2D>& out) const
{
    out.clear();

    ScratchCandidates.clear();
    GatherCandidates(box, ScratchCandidates);
    const auto& candidates = ScratchCandidates;

    for (uint32_t idx : candidates)
    {
        const ColliderEntry& entry = Entries[idx];
        if (!entry.Live) continue;
        if (!entry.Shape.WorldBounds.Intersects(box)) continue;

        // Deduplicate (grid cells can reference the same entry multiple times)
        bool duplicate = false;
        for (const PhysicsHandle2D& h : out)
        {
            if (h == entry.Handle) { duplicate = true; break; }
        }
        if (!duplicate)
            out.push_back(entry.Handle);
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

    Vec2d halfExt = box.HalfExtent();

    for (uint32_t idx : candidates)
    {
        const ColliderEntry& entry = Entries[idx];
        if (!entry.Live || !entry.Shape.WorldBounds.IsValid()) continue;

        // Minkowski-expand the static collider by the mover's half-extents
        const Aabb2d& s = entry.Shape.WorldBounds;
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
            best.Handle = entry.Handle;
            best.DidHit = true;
        }
    }

    return best;
}

MoveResult2D PhysicsDomain2D::MoveBox(const Aabb2d& box, Vec2d desiredDelta) const
{
    MoveResult2D result;

    // Resolve X first
    bool hitRight = false, hitLeft = false;
    float safeX = ResolveAxis(box, static_cast<float>(desiredDelta.X),
                              /*isVertical=*/false, hitRight, hitLeft);

    // Shift box by safe X before resolving Y
    Aabb2d boxAfterX = Aabb2d::FromCenterHalfExtent(
        Vec2d{ box.Center().X + safeX, box.Center().Y },
        box.HalfExtent());

    // Resolve Y
    bool hitUp = false, hitDown = false;
    float safeY = ResolveAxis(boxAfterX, static_cast<float>(desiredDelta.Y),
                              /*isVertical=*/true, hitUp, hitDown);

    result.ResolvedDelta = { safeX, safeY };
    result.HitFloor      = hitDown;
    result.HitCeiling    = hitUp;
    result.HitWall       = hitLeft || hitRight;

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
                                   bool& hitPositive, bool& hitNegative) const
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

    float safe = delta;

    for (uint32_t idx : candidates)
    {
        const ColliderEntry& entry = Entries[idx];
        if (!entry.Live || !entry.Shape.WorldBounds.IsValid()) continue;

        const Aabb2d& s = entry.Shape.WorldBounds;

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
