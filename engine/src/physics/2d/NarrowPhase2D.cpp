#include <physics/2d/NarrowPhase2D.h>

#include <cmath>

// ---------------------------------------------------------------------------
// SweepCircleVsAabb
// ---------------------------------------------------------------------------

CircleContact SweepCircleVsAabb(Vec2d center, float radius,
                                Vec2d velocity, const Aabb2d& aabb)
{
    CircleContact result; // TOI = 2.0f (no hit) by default

    const float speedSq = velocity.X * velocity.X + velocity.Y * velocity.Y;
    if (speedSq < 1e-12f) return result;

    // -------------------------------------------------------------------------
    // Face tests — sweep center against each expanded face plane.
    // Valid when the hit point lies within the face's perpendicular extent.
    // -------------------------------------------------------------------------
    struct FaceTest
    {
        float planeCoord; // face position on the sweep axis, expanded by radius
        float rangeMin;   // valid range on the perpendicular axis
        float rangeMax;
        bool  xAxis;      // true = axis is X (left/right faces)
        Vec2d normal;
    };

    const FaceTest faces[4] = {
        { aabb.Min.X - radius, aabb.Min.Y, aabb.Max.Y, true,  { -1.0f,  0.0f } }, // left
        { aabb.Max.X + radius, aabb.Min.Y, aabb.Max.Y, true,  {  1.0f,  0.0f } }, // right
        { aabb.Min.Y - radius, aabb.Min.X, aabb.Max.X, false, {  0.0f, -1.0f } }, // bottom
        { aabb.Max.Y + radius, aabb.Min.X, aabb.Max.X, false, {  0.0f,  1.0f } }, // top
    };

    for (const auto& face : faces)
    {
        float dAxis = face.xAxis ? velocity.X : velocity.Y;
        float pAxis = face.xAxis ? center.X   : center.Y;
        float dPerp = face.xAxis ? velocity.Y : velocity.X;
        float pPerp = face.xAxis ? center.Y   : center.X;

        if (std::abs(dAxis) < 1e-8f) continue;

        float t = (face.planeCoord - pAxis) / dAxis;
        if (t < 0.0f || t > 1.0f || t >= result.TOI) continue;

        float perpHit = pPerp + dPerp * t;
        if (perpHit < face.rangeMin || perpHit > face.rangeMax) continue;

        result.TOI    = t;
        result.Normal = face.normal;
    }

    // -------------------------------------------------------------------------
    // Corner tests — ray vs. circle at each AABB corner.
    // Accepted only when the circle center at time t is in the corner's
    // voronoi region (outside the AABB on both axes from that corner).
    // Normal is radial from the corner to the circle center at contact.
    // -------------------------------------------------------------------------
    const Vec2d corners[4] = {
        { aabb.Min.X, aabb.Min.Y }, // bottom-left
        { aabb.Max.X, aabb.Min.Y }, // bottom-right
        { aabb.Min.X, aabb.Max.Y }, // top-left
        { aabb.Max.X, aabb.Max.Y }, // top-right
    };

    const float r2 = radius * radius;

    for (int i = 0; i < 4; ++i)
    {
        Vec2d oc = { center.X - corners[i].X, center.Y - corners[i].Y };
        float a  = speedSq;
        float b  = 2.0f * (oc.X * velocity.X + oc.Y * velocity.Y);
        float c  = oc.X * oc.X + oc.Y * oc.Y - r2;

        float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) continue;

        float t = (-b - std::sqrt(disc)) / (2.0f * a);
        if (t < 0.0f || t > 1.0f || t >= result.TOI) continue;

        Vec2d hit = { center.X + velocity.X * t, center.Y + velocity.Y * t };

        bool inVoronoi;
        switch (i)
        {
            case 0: inVoronoi = hit.X < aabb.Min.X && hit.Y < aabb.Min.Y; break;
            case 1: inVoronoi = hit.X > aabb.Max.X && hit.Y < aabb.Min.Y; break;
            case 2: inVoronoi = hit.X < aabb.Min.X && hit.Y > aabb.Max.Y; break;
            case 3: inVoronoi = hit.X > aabb.Max.X && hit.Y > aabb.Max.Y; break;
            default: inVoronoi = false;
        }
        if (!inVoronoi) continue;

        Vec2d n   = { hit.X - corners[i].X, hit.Y - corners[i].Y };
        float len = std::sqrt(n.X * n.X + n.Y * n.Y);
        if (len > 1e-8f) { n.X /= len; n.Y /= len; }

        result.TOI    = t;
        result.Normal = n;
    }

    return result;
}

// ---------------------------------------------------------------------------
// IsGhostEdge
// ---------------------------------------------------------------------------

bool IsGhostEdge(Vec2d surfaceNormal, const CollisionGrid2D& grid,
                 int col, int row)
{
    if (surfaceNormal.Y >  0.5f) return grid.IsSolid(col,     row + 1); // top face
    if (surfaceNormal.Y < -0.5f) return grid.IsSolid(col,     row - 1); // bottom face
    if (surfaceNormal.X >  0.5f) return grid.IsSolid(col + 1, row    ); // right face
    if (surfaceNormal.X < -0.5f) return grid.IsSolid(col - 1, row    ); // left face

    // Corner normal — suppress if either face-adjacent neighbor is solid
    bool neighborX = (surfaceNormal.X < 0.0f) ? grid.IsSolid(col - 1, row)
                                               : grid.IsSolid(col + 1, row);
    bool neighborY = (surfaceNormal.Y < 0.0f) ? grid.IsSolid(col, row - 1)
                                               : grid.IsSolid(col, row + 1);
    return neighborX || neighborY;
}
