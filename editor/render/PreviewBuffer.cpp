#include "PreviewBuffer.h"

void PreviewBuffer::SetBox(Vec3d center, Vec3d halfExtents)
{
    Box = PreviewBox{ center, halfExtents };
}

void PreviewBuffer::Clear()
{
    Box.reset();
}

const std::optional<PreviewBox>& PreviewBuffer::GetBox() const
{
    return Box;
}
