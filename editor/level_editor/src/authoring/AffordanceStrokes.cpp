#include "authoring/AffordanceStrokes.h"

void AppendLine(ViewportAffordanceOutput& output, Vec3d a, Vec3d b,
                Vec4 color, float width)
{
    output.Lines.push_back({ a, b, color, width });
}

void AppendQuad(ViewportAffordanceOutput& output, Vec3d a, Vec3d b,
                Vec3d c, Vec3d d, Vec4 color)
{
    output.FillTriangles.push_back({ a, color });
    output.FillTriangles.push_back({ b, color });
    output.FillTriangles.push_back({ c, color });
    output.FillTriangles.push_back({ a, color });
    output.FillTriangles.push_back({ c, color });
    output.FillTriangles.push_back({ d, color });
}

void AppendArrow(ViewportAffordanceOutput& output, Vec3d from, Vec3d to,
                 Vec3d wingAxis, Vec4 color)
{
    AppendLine(output, from, to, color, 2.0f);
    Vec3d direction = to - from;
    if (direction.SqrMagnitude() <= 1.0e-8f)
        return;
    direction = direction.Normalized();
    const Vec3d base = to - direction * 0.28f;
    const Vec3d wing = wingAxis.Normalized() * 0.16f;
    AppendLine(output, to, base + wing, color, 2.0f);
    AppendLine(output, to, base - wing, color, 2.0f);
}
