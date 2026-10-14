#include "BrushClipCut.h"

#include "brush/BrushValidation.h"

ClipCut CutBrush(const BrushMesh& mesh, const Plane& plane, BrushOps::ClipCap cap)
{
    ClipCut cut;
    // Missed is a fact about the brush, not about the kernel's answer: the
    // plane crosses it only if it has vertices strictly on both sides.
    bool anyFront = false;
    bool anyBack = false;
    for (const BrushVertex& vertex : mesh.Vertices)
    {
        const float d = plane.SignedDistanceTo(vertex.Position);
        anyFront = anyFront || d > BrushOps::kClipSnap;
        anyBack = anyBack || d < -BrushOps::kClipSnap;
    }
    if (!anyFront || !anyBack)
        return cut;

    cut.Front = BrushOps::Clip(mesh, plane, true, cap);
    cut.Back = BrushOps::Clip(mesh, plane, false, cap);
    // A crossing plane with an empty half is a cut the kernel could not
    // complete, not a miss. Each stage names itself when it refuses.
    BrushMesh originalCheck = mesh;
    BrushMesh frontCheck = cut.Front;
    BrushMesh backCheck = cut.Back;
    const bool wasClosed = cap == BrushOps::ClipCap::Capped && BrushValidateAndRepair(originalCheck).Closed;
    const BrushRepairResult frontReport = BrushValidateAndRepair(frontCheck);
    const BrushRepairResult backReport = BrushValidateAndRepair(backCheck);
    const auto stage = [&](const char* name, const BrushMesh& half, const BrushRepairResult& report) -> std::string {
        if (half.Faces.empty())
            return std::string(name) + " kernel refused";
        if (!report.Ok)
            return std::string(name) + " not usable";
        if (wasClosed && !report.Closed)
            return std::string(name) + " open";
        return {};
    };
    cut.Why = stage("front", cut.Front, frontReport);
    if (cut.Why.empty())
        cut.Why = stage("back", cut.Back, backReport);
    cut.Outcome = cut.Why.empty() ? ClipCut::Result::Valid : ClipCut::Result::Invalid;
    return cut;
}

std::vector<BrushMesh> KeptShells(const ClipCut& cut, ClipMode mode)
{
    std::vector<BrushMesh> shells;
    if (cut.Outcome != ClipCut::Result::Valid)
        return shells;
    if (mode == ClipMode::Split || mode == ClipMode::KeepFront)
        for (BrushMesh& shell : BrushConnectedComponents(cut.Front))
            shells.push_back(std::move(shell));
    if (mode == ClipMode::Split || mode == ClipMode::KeepBack)
        for (BrushMesh& shell : BrushConnectedComponents(cut.Back))
            shells.push_back(std::move(shell));
    return shells;
}
