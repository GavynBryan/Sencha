#pragma once

#include <math/Vec.h>

#include <optional>

struct PreviewBox
{
    Vec3d Center;
    Vec3d HalfExtents;
};

class PreviewBuffer
{
public:
    void SetBox(Vec3d center, Vec3d halfExtents);
    void Clear();
    [[nodiscard]] const std::optional<PreviewBox>& GetBox() const;

private:
    std::optional<PreviewBox> Box;
};
