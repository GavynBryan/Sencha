#include <render/ScreenBounds.h>

#include <algorithm>

void NdcBounds::Include(const NdcBounds& other)
{
    if (WholeView || other.IsEmpty())
        return;
    if (other.WholeView)
    {
        *this = other;
        return;
    }
    for (int axis = 0; axis < 2; ++axis)
    {
        Min[axis] = std::min(Min[axis], other.Min[axis]);
        Max[axis] = std::max(Max[axis], other.Max[axis]);
    }
}

NdcBounds ProjectToNdc(const Aabb3d& box, const Mat4& viewProjection)
{
    NdcBounds bounds;
    int behind = 0;
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec4 clip = viewProjection
            * Vec4((corner & 1) != 0 ? box.Max.X : box.Min.X, (corner & 2) != 0 ? box.Max.Y : box.Min.Y,
                   (corner & 4) != 0 ? box.Max.Z : box.Min.Z, 1.0f);
        if (clip.W <= 1e-5f)
        {
            ++behind;
            continue;
        }
        bounds.Min[0] = std::min(bounds.Min[0], clip.X / clip.W);
        bounds.Min[1] = std::min(bounds.Min[1], clip.Y / clip.W);
        bounds.Max[0] = std::max(bounds.Max[0], clip.X / clip.W);
        bounds.Max[1] = std::max(bounds.Max[1], clip.Y / clip.W);
    }
    if (behind == 8)
        return NdcBounds{};
    if (behind > 0)
        return NdcBounds{ .Min = { -1.0f, -1.0f }, .Max = { 1.0f, 1.0f }, .WholeView = true };
    return bounds;
}
