#include "PreviewBuffer.h"

#include <utility>

void PreviewBuffer::SetMesh(const Transform3f& transform, BrushMesh mesh, Vec4 color)
{
    Preview = PreviewMesh{ transform, std::move(mesh), color };
}

void PreviewBuffer::Clear()
{
    Preview.reset();
}

const std::optional<PreviewMesh>& PreviewBuffer::GetMesh() const
{
    return Preview;
}
