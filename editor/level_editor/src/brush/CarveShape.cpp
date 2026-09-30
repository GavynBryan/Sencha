#include "brush/CarveShape.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace
{
struct Box
{
    float MinX, MinY, MaxX, MaxY;
};

Box Normalize(Vec2d a, Vec2d b)
{
    return Box{ std::min(a.X, b.X), std::min(a.Y, b.Y), std::max(a.X, b.X), std::max(a.Y, b.Y) };
}

// Below this the turned rectangle is on its diagonal and the simultaneous solve
// below has to be taken by name. See LargestInscribedRotatedRectangle.
constexpr double kDiagonalEpsilon = 1e-6;

CarveShapeFrame FrameFor(const Box& box, float orientation)
{
    return CarveShapeFrameFor(Vec2d{ box.MinX, box.MinY }, Vec2d{ box.MaxX, box.MaxY }, orientation);
}

std::vector<Vec2d> Rectangle(const CarveShapeFrame& frame)
{
    return { frame.ToBox(Vec2d{ -frame.SemiU, -frame.SemiV }),
             frame.ToBox(Vec2d{ frame.SemiU, -frame.SemiV }),
             frame.ToBox(Vec2d{ frame.SemiU, frame.SemiV }),
             frame.ToBox(Vec2d{ -frame.SemiU, frame.SemiV }) };
}
}

CarveShapeFrame CarveShapeFrameFor(Vec2d boxMin, Vec2d boxMax, float orientation)
{
    const Box box = Normalize(boxMin, boxMax);
    const float p = (box.MaxX - box.MinX) * 0.5f;
    const float q = (box.MaxY - box.MinY) * 0.5f;
    const Vec2d semi = LargestInscribedRotatedRectangle(p, q, orientation);
    return CarveShapeFrame{ Vec2d{ (box.MinX + box.MaxX) * 0.5f, (box.MinY + box.MaxY) * 0.5f },
                            semi.X, semi.Y, std::sin(orientation), std::cos(orientation) };
}

Vec2d LargestInscribedRotatedRectangle(float p, float q, float angle)
{
    if (!(p > 0.0f) || !(q > 0.0f))
        return Vec2d{ 0.0f, 0.0f };

    // Taking absolute values is the angle normalization: (c, s) is unchanged by
    // adding any half turn and mirrors the quarter-turn symmetry, so the solve
    // never sees an angle outside the first quadrant. The arithmetic is double
    // because the branch below divides by a difference of two cosines.
    const double c = std::abs(std::cos(static_cast<double>(angle)));
    const double s = std::abs(std::sin(static_cast<double>(angle)));
    const double k = 2.0 * c * s; // |sin 2*angle|

    // The objective is quasi-concave and the region is a convex polygon, so the
    // maximum is where one constraint binds or where both do.
    if (p <= q * k)
        return Vec2d{ static_cast<float>(p / (2.0 * c)), static_cast<float>(p / (2.0 * s)) };
    if (q <= p * k)
        return Vec2d{ static_cast<float>(q / (2.0 * s)), static_cast<float>(q / (2.0 * c)) };

    // On the diagonal, by name. Over the reals this branch is unreachable at
    // c == s, because there k == 1 and min(p/q, q/p) <= 1 makes one of the tests
    // above true. In floating point it is reachable: an eighth turn leaves c and
    // s a hair apart while k rounds to just under one, so a square box falls
    // through both tests and lands here with a divisor of about 3e-8.
    //
    // The divisor is small but its numerator is not merely small, it is exactly
    // zero, because reaching here forces p and q to agree to within 1 - k and no
    // two distinct floats are that close. So the solve below would survive on
    // this platform. The guard is here because it need not: cos and sin are not
    // required to be correctly rounded, so a library returning the same value for
    // both at some angle would divide zero by zero, and this function's contract
    // should not rest on which libm it was built against.
    //
    // The value it returns is the true answer rather than a fallback. At c == s
    // the constraints collapse to a + b <= sqrt(2) min(p, q), whose maximum-area
    // point is this; and it satisfies both constraints for any p and q at any
    // angle, so a guard that fired early still cannot hand back a rectangle that
    // leaves the box.
    if (std::abs(c - s) < kDiagonalEpsilon)
    {
        const float half = static_cast<float>(std::min(p, q) / (c + s));
        return Vec2d{ half, half };
    }

    // Both bind. Taken as sum and difference rather than by Cramer's rule: c + s
    // is never below 1, and the only small divisor, c - s, carries a numerator
    // that the branch conditions force to shrink with it.
    const double sum = (p + q) / (c + s);
    const double difference = (p - q) / (c - s);
    return Vec2d{ static_cast<float>(0.5 * (sum + difference)),
                  static_cast<float>(0.5 * (sum - difference)) };
}

CarveShapeLimits CarveShapeRange(Vec2d boxMin, Vec2d boxMax, float rise, float orientation,
                                 float weldTol)
{
    const Box box = Normalize(boxMin, boxMax);
    // Measured against the turned semi-axes, because a turn changes how much
    // room the arc has and therefore how short its chords get.
    const CarveShapeFrame frame = FrameFor(box, orientation);
    const float semiX = frame.SemiU;
    const float semiY = frame.SemiV * 2.0f * std::clamp(rise, 0.0f, 1.0f);
    const float semiMinor = std::min(semiX, semiY);

    CarveShapeLimits limits;
    limits.MinSegments = 2;
    if (semiMinor <= 0.0f)
    {
        limits.MaxSegments = limits.MinSegments - 1; // no arc at all
        return limits;
    }

    // Two real limits on how finely the arc can be sampled. A chord below the
    // weld tolerance is welded away, leaving a vertex the outline did not ask
    // for. A chord near the float resolution of coordinates this far from the
    // origin cannot be placed accurately enough for the first check to mean
    // anything, so the budget carries that noise as well.
    const float magnitude = std::max({ std::abs(box.MinX), std::abs(box.MaxX), std::abs(box.MinY),
                                       std::abs(box.MaxY) });
    const float budget = weldTol + 16.0f * std::numeric_limits<float>::epsilon() * magnitude;
    if (budget <= 0.0f)
    {
        limits.MaxSegments = std::numeric_limits<int>::max();
        return limits;
    }

    // The chord between two samples an angle d apart is
    // 2 sin(d/2) * sqrt(a^2 sin^2(m) + b^2 cos^2(m)) for the midpoint angle m,
    // whose smallest value over the arc is 2 sin(d/2) times the minor semi-axis.
    // Sampling uniformly gives d = pi/segments, so the shortest chord clears the
    // budget while segments <= pi / (2 asin(budget / (2 * minor))).
    const float sine = budget / (2.0f * semiMinor);
    if (sine >= 1.0f)
    {
        limits.MaxSegments = limits.MinSegments - 1; // even a single segment welds away
        return limits;
    }
    const float cap = std::numbers::pi_v<float> / (2.0f * std::asin(sine));
    limits.MaxSegments = cap >= static_cast<float>(std::numeric_limits<int>::max())
                             ? std::numeric_limits<int>::max()
                             : static_cast<int>(cap);
    return limits;
}

std::vector<Vec2d> CarveShapeOutline(CarveShape shape, Vec2d boxMin, Vec2d boxMax,
                                     const CarveShapeParams& params)
{
    // Built in the shape's own turned frame, where its rise runs along +y, then
    // carried back into box coordinates. At a quarter turn the frame's extents
    // are the box's with the two swapped, so the outline still covers the box.
    const CarveShapeFrame frame = FrameFor(Normalize(boxMin, boxMax), params.Orientation);
    if (shape == CarveShape::Rectangle || params.ArchRise <= 0.0f || params.ArchSegments < 2)
        return Rectangle(frame);

    // Computed rather than subtracted at full rise, so the springline lands
    // exactly on the floor and the jambs vanish instead of collapsing into
    // edges a weld would have to clean up.
    const bool springsFromTheFloor = params.ArchRise >= 1.0f;
    const float springY = frame.SpringlineAt(params.ArchRise);
    const float semiY = frame.SemiV - springY;

    std::vector<Vec2d> outline;
    outline.reserve(static_cast<std::size_t>(params.ArchSegments) + 3);
    if (!springsFromTheFloor)
    {
        outline.push_back(frame.ToBox(Vec2d{ -frame.SemiU, -frame.SemiV }));
        outline.push_back(frame.ToBox(Vec2d{ frame.SemiU, -frame.SemiV }));
    }
    // Right springline round to the left one, so the arc's own ends are the
    // jamb tops and no duplicate vertex is introduced at the junction.
    for (int i = 0; i <= params.ArchSegments; ++i)
    {
        const float theta = std::numbers::pi_v<float> * static_cast<float>(i)
                            / static_cast<float>(params.ArchSegments);
        outline.push_back(frame.ToBox(Vec2d{ frame.SemiU * std::cos(theta),
                                             springY + semiY * std::sin(theta) }));
    }
    return outline;
}
