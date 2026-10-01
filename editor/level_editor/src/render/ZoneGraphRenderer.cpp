#include "ZoneGraphRenderer.h"

#include "EditorTheme.h"

#include "document/WorldDocument.h"
#include "document/ZoneGraph.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr int kLinkDashes = 12;

Vec4 ColorOf(const ZoneGraphEdge& edge)
{
    if (edge.Failed)
        return EditorTheme::ProblemError;
    if (edge.Warned)
        return EditorTheme::ProblemWarning;
    if (edge.CrossGraph)
        return EditorTheme::ZoneGraphCrossGraph;
    return edge.Connection == ZoneGraphConnection::Dock ? EditorTheme::TransitionLine : EditorTheme::ZoneGraphLink;
}

// Two strokes meeting at `tip`, opening back toward `tail`.
void AppendArrowhead(std::vector<EditorLineSegment>& segments, Vec3d tip, Vec3d tail, Vec4 color)
{
    const Vec3d along = tip - tail;
    const float length = along.Magnitude();
    if (length <= 1e-4f)
        return;
    const Vec3d direction = along / length;
    Vec3d side = direction.Cross(Vec3d{ 0.0f, 1.0f, 0.0f });
    if (side.SqrMagnitude() < 1e-6f)
        side = direction.Cross(Vec3d{ 1.0f, 0.0f, 0.0f });
    side = side.Normalized();
    const float size = std::min(0.75f, length * 0.12f);
    const Vec3d base = tip - direction * size;
    segments.push_back({ tip, base + side * (size * 0.5f), color, EditorTheme::OverlayLinePixels });
    segments.push_back({ tip, base - side * (size * 0.5f), color, EditorTheme::OverlayLinePixels });
}
} // namespace

ZoneGraphRenderer::ZoneGraphRenderer(EditorWideLinePipeline& lines)
    : Lines(lines)
{
}

void ZoneGraphRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                     const CameraRenderData& camera, const WorldDocument& world)
{
    std::vector<EditorLineSegment> segments;
    for (const ZoneGraphEdge& edge : ZoneGraphEdges(world))
    {
        const Vec4 color = ColorOf(edge);
        const float width = EditorTheme::OverlayLinePixels * 2.0f;
        if (edge.Connection == ZoneGraphConnection::Dock)
            segments.push_back({ edge.From, edge.To, color, width });
        else
            for (int dash = 0; dash < kLinkDashes; dash += 2)
            {
                const float t0 = static_cast<float>(dash) / kLinkDashes;
                const float t1 = static_cast<float>(dash + 1) / kLinkDashes;
                segments.push_back({ edge.From + (edge.To - edge.From) * t0, edge.From + (edge.To - edge.From) * t1,
                                     color, width });
            }
        if (edge.AToB && !edge.BToA)
            AppendArrowhead(segments, edge.To, edge.From, color);
        else if (edge.BToA && !edge.AToB)
            AppendArrowhead(segments, edge.From, edge.To, color);
    }
    if (!segments.empty())
        Lines.Submit(frame, viewport.Region(), camera, segments, /*onTop*/ true, "ZoneGraphRenderer");
}
