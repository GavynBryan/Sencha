#pragma once

#include <math/geometry/3d/Ray3d.h>
#include "../meshedit/MeshElementKind.h"
#include "../selection/SelectableRef.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <vector>

struct EditorViewport;
class LevelScene;

enum class BrushPickMode : uint8_t
{
    EntityOnly = 0,
    FacePreferred = 1,
    FaceOnly = 2,
    EdgeOnly = 3,
    VertexOnly = 4,
};

struct BrushPickRequest
{
    BrushPickMode Mode = BrushPickMode::EntityOnly;
};

class PickingService
{
public:
    [[nodiscard]] SelectableRef Pick(const EditorViewport& viewport,
                                     ImVec2 point,
                                     const LevelScene& scene,
                                     BrushPickRequest request = {}) const;
    [[nodiscard]] std::optional<Vec3d> ProjectPointToGrid(const EditorViewport& viewport,
                                                          ImVec2 point) const;

    // Rubber-band selection: every element of the given mode whose projection
    // falls in the screen rectangle. Entities by projected-bounds overlap;
    // vertices/edges/faces by projected point (position/midpoint/center) inside.
    [[nodiscard]] std::vector<SelectableRef> PickInRect(const EditorViewport& viewport,
                                                        ImVec2 rectMin,
                                                        ImVec2 rectMax,
                                                        const LevelScene& scene,
                                                        MeshElementKind mode) const;

private:
    struct PickCandidate
    {
        SelectableRef Ref = {};
        float Distance = 0.0f;
        uint8_t Priority = 0;
    };

    [[nodiscard]] SelectableRef PickBrushElement(const Ray3d& ray,
                                                 const LevelScene& scene,
                                                 BrushPickRequest request) const;
    [[nodiscard]] static bool AllowsEntities(BrushPickRequest request);
    [[nodiscard]] static bool AllowsFaces(BrushPickRequest request);
    [[nodiscard]] static uint8_t PriorityFor(BrushPickRequest request, SelectableKind kind);
    [[nodiscard]] static bool IsBetterCandidate(const PickCandidate& candidate,
                                                const PickCandidate& best,
                                                bool hasBest);
    [[nodiscard]] std::optional<PickCandidate> MakeBrushBodyCandidate(const Ray3d& ray,
                                                                      const LevelScene& scene,
                                                                      EntityId entity) const;
    void GatherBrushFaceCandidates(const Ray3d& ray,
                                   const LevelScene& scene,
                                   EntityId entity,
                                   std::vector<PickCandidate>& outCandidates) const;

    // Screen-space picking for edge/vertex modes: project mesh elements to the
    // viewport and select the nearest within a pixel threshold, breaking ties by
    // depth.
    [[nodiscard]] SelectableRef PickEdge(const EditorViewport& viewport,
                                         ImVec2 point,
                                         const LevelScene& scene) const;
    [[nodiscard]] SelectableRef PickVertex(const EditorViewport& viewport,
                                           ImVec2 point,
                                           const LevelScene& scene) const;

    [[nodiscard]] Ray3d BuildRay(const EditorViewport& viewport, ImVec2 point) const;
};
