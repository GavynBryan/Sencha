#include "PreviewBuffer.h"

#include <utility>

void PreviewBuffer::SetMesh(const Transform3f& transform, BrushMesh mesh)
{
    Preview = PreviewMesh{ transform, std::move(mesh) };
}

void PreviewBuffer::Clear()
{
    Preview.reset();
}

const std::optional<PreviewMesh>& PreviewBuffer::GetMesh() const
{
    return Preview;
}
