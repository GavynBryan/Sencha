#pragma once

#include <math/geometry/3d/Ray3d.h>
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
    [[nodiscard]] Ray3d BuildRay(const EditorViewport& viewport, ImVec2 point) const;
};
