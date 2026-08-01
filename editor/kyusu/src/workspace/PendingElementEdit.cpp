#include "PendingElementEdit.h"

#include "brush/BrushOps.h"
#include "commands/CommandStack.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshElements.h"

#include <algorithm>
#include <optional>
#include <utility>

BrushMesh PendingElementEdit::BuildEdited(Phase stage, const Capture& capture,
                                          float distance, float width, int segments)
{
    return stage == Phase::Inset
        ? BrushOps::ExtrudeFacesAlongNormals(capture.Original, capture.Faces, distance)
        : BrushOps::BevelEdges(capture.Original, capture.Edges, width, segments);
}

void PendingElementEdit::BeginInset(ManipulationSink& target, CommandStack& commands,
                                    std::span<const SelectableRef> selection, float distance)
{
    Cancel(commands); // a re-inset replaces the previous preview

    std::vector<Capture> captures;
    for (const SelectableRef& ref : selection)
    {
        if (!ref.IsFace())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = target.ResolveMesh(ref.Entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr
            || ref.ElementId >= resolved->Mesh->Faces.size())
            continue;
        Capture* capture = nullptr;
        for (Capture& existing : captures)
            if (existing.Entity == ref.Entity)
                capture = &existing;
        if (capture == nullptr)
        {
            captures.push_back(Capture{ ref.Entity, *resolved->Mesh, {}, {} });
            capture = &captures.back();
        }
        capture->Faces.push_back(ref.ElementId);
    }
    if (captures.empty())
        return;

    Stage = Phase::Inset;
    Target = &target;
    Captures = std::move(captures);
    Distance = distance;
    commands.OpenPendingEdit([this, &commands] { Cancel(commands); });
    Regenerate();
}

void PendingElementEdit::BeginBevel(ManipulationSink& target, CommandStack& commands,
                                    std::span<const SelectableRef> selection, float width, int segments)
{
    Cancel(commands);

    // Resolve the selected edge refs into vertex pairs against the captured
    // originals, building each entity's edge enumeration once.
    std::vector<Capture> captures;
    EntityId cachedEntity = {};
    std::vector<EdgeElement> cachedEdges;
    for (const SelectableRef& ref : selection)
    {
        if (!ref.IsEdge())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = target.ResolveMesh(ref.Entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            continue;
        if (!(cachedEntity == ref.Entity))
        {
            cachedEntity = ref.Entity;
            cachedEdges = MeshElements::Edges(*resolved->Mesh, resolved->Transform);
        }
        if (ref.ElementId >= cachedEdges.size())
            continue;
        Capture* capture = nullptr;
        for (Capture& existing : captures)
            if (existing.Entity == ref.Entity)
                capture = &existing;
        if (capture == nullptr)
        {
            captures.push_back(Capture{ ref.Entity, *resolved->Mesh, {}, {} });
            capture = &captures.back();
        }
        capture->Edges.push_back({ cachedEdges[ref.ElementId].VertexA,
                                   cachedEdges[ref.ElementId].VertexB });
    }
    if (captures.empty())
        return;

    Stage = Phase::Bevel;
    Target = &target;
    Captures = std::move(captures);
    Width = width;
    Segments = std::clamp(segments, 1, 16);
    commands.OpenPendingEdit([this, &commands] { Cancel(commands); });
    Regenerate();
}

void PendingElementEdit::SetInsetDistance(float distance)
{
    if (Stage != Phase::Inset || distance == Distance)
        return;
    Distance = distance;
    Regenerate();
}

void PendingElementEdit::SetBevelParams(float width, int segments)
{
    if (Stage != Phase::Bevel)
        return;
    segments = std::clamp(segments, 1, 16);
    if (width == Width && segments == Segments)
        return;
    Width = width;
    Segments = segments;
    Regenerate();
}

void PendingElementEdit::Regenerate()
{
    if (Stage == Phase::Idle || Target == nullptr)
        return;
    for (const Capture& capture : Captures)
    {
        if (!Target->ResolveMesh(capture.Entity).has_value())
            continue;
        const BrushMesh edited = BuildEdited(Stage, capture, Distance, Width, Segments);
        const bool changed = edited.Vertices.size() != capture.Original.Vertices.size()
                          || edited.Faces.size() != capture.Original.Faces.size();
        Target->PreviewMesh(capture.Entity, changed ? edited : capture.Original);
    }
}

void PendingElementEdit::Commit(CommandStack& commands)
{
    if (Stage == Phase::Idle || Target == nullptr)
        return;

    const Phase stage = Stage;
    ManipulationSink& target = *Target;
    std::vector<Capture> captures = std::move(Captures);
    const float distance = Distance;
    const float width = Width;
    const int segments = Segments;

    commands.ClosePendingEdit();
    Stage = Phase::Idle;
    ClearCapture();

    std::vector<MeshEdit> edits;
    for (Capture& capture : captures)
    {
        if (!target.ResolveMesh(capture.Entity).has_value())
            continue; // the entity died while pending
        BrushMesh after = BuildEdited(stage, capture, distance, width, segments);
        if (after.Vertices.size() == capture.Original.Vertices.size()
            && after.Faces.size() == capture.Original.Faces.size())
            continue;
        edits.push_back(MeshEdit{ capture.Entity, std::move(capture.Original), std::move(after) });
    }
    if (edits.empty())
        return;
    target.CommitMeshes(std::move(edits));

    // Beveled edges no longer exist: drop the stale element refs. Inset caps
    // keep their indices, so the face selection stays valid.
    if (stage == Phase::Bevel)
        target.SelectElements({});
}

void PendingElementEdit::Cancel(CommandStack& commands)
{
    if (Stage == Phase::Idle)
        return;
    commands.ClosePendingEdit();
    Stage = Phase::Idle;
    if (Target != nullptr)
        for (const Capture& capture : Captures)
            if (Target->ResolveMesh(capture.Entity).has_value())
                Target->PreviewMesh(capture.Entity, capture.Original);
    ClearCapture();
}

void PendingElementEdit::ClearCapture()
{
    Target = nullptr;
    Captures.clear();
    Distance = 0.0f;
    Width = 0.0f;
    Segments = 1;
}
