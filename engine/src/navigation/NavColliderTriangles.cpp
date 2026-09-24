#include "NavColliderTriangles.h"

#include <cmath>
#include <numbers>

namespace
{
    void AddTriangle(std::vector<Vec3d>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c)
    {
        out.insert(out.end(), { a, b, c });
    }

    void TriangulateBox(const Vec3d& h, std::vector<Vec3d>& out)
    {
        const Vec3d c[8] = {
            { -h.X, -h.Y, -h.Z }, { h.X, -h.Y, -h.Z }, { h.X, -h.Y, h.Z }, { -h.X, -h.Y, h.Z },
            { -h.X, h.Y, -h.Z },  { h.X, h.Y, -h.Z },  { h.X, h.Y, h.Z },  { -h.X, h.Y, h.Z },
        };
        // Each face's corners counter-clockwise seen from outside.
        constexpr int kFaces[6][4] = {
            { 4, 7, 6, 5 }, { 0, 1, 2, 3 }, { 0, 4, 5, 1 },
            { 3, 2, 6, 7 }, { 0, 3, 7, 4 }, { 1, 5, 6, 2 },
        };
        for (const auto& f : kFaces)
        {
            AddTriangle(out, c[f[0]], c[f[1]], c[f[2]]);
            AddTriangle(out, c[f[0]], c[f[2]], c[f[3]]);
        }
    }

    // A latitude/longitude hull. A capsule is a sphere split at the equator,
    // its halves pushed apart by the cylinder, with the cylinder between them.
    void TriangulateRounded(float radius, float halfHeight, std::vector<Vec3d>& out)
    {
        constexpr int kRings = 6;
        constexpr int kSegments = 8;
        const auto point = [&](int ring, int segment)
        {
            const float polar = std::numbers::pi_v<float> * static_cast<float>(ring) / kRings;
            const float azimuth =
                2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / kSegments;
            const float ringRadius = std::sin(polar) * radius;
            const float lift = ring <= kRings / 2 ? halfHeight : -halfHeight;
            return Vec3d(ringRadius * std::cos(azimuth), std::cos(polar) * radius + lift,
                         -ringRadius * std::sin(azimuth));
        };
        for (int ring = 0; ring < kRings; ++ring)
            for (int segment = 0; segment < kSegments; ++segment)
            {
                const Vec3d a = point(ring, segment);
                const Vec3d b = point(ring + 1, segment);
                const Vec3d c = point(ring + 1, segment + 1);
                const Vec3d d = point(ring, segment + 1);
                if (ring > 0)
                    AddTriangle(out, a, b, d);
                if (ring < kRings - 1)
                    AddTriangle(out, d, b, c);
            }
        if (halfHeight <= 0.0f)
            return;
        const Vec3d down(0.0f, 2.0f * halfHeight, 0.0f);
        for (int segment = 0; segment < kSegments; ++segment)
        {
            const Vec3d top0 = point(kRings / 2, segment);
            const Vec3d top1 = point(kRings / 2, segment + 1);
            AddTriangle(out, top0, top0 - down, top1);
            AddTriangle(out, top1, top0 - down, top1 - down);
        }
    }
}

void TriangulateCollider(const CollisionShape& shape, std::vector<Vec3d>& vertices)
{
    switch (shape.Type)
    {
    case CollisionShapeType::Box:
        TriangulateBox(shape.HalfExtents, vertices);
        return;
    case CollisionShapeType::Sphere:
        TriangulateRounded(shape.Radius, 0.0f, vertices);
        return;
    case CollisionShapeType::Capsule:
        TriangulateRounded(shape.Radius, shape.HalfHeight, vertices);
        return;
    }
}
