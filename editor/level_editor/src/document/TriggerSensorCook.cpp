#include "TriggerSensorCook.h"

#include "EditorScene.h"
#include "brush/BrushBox.h"

#include <logic/Trigger.h>

#include <format>

bool CollectTriggerSensors(const EditorScene& scene, std::vector<CookTriggerSensor>& sensors,
                          std::vector<std::string>& errors)
{
    const World& world = scene.GetRegistry().Components;
    const auto before = errors.size();
    if (!world.IsRegistered<Trigger>())
        return true;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!world.HasComponent<Trigger>(entity) || scene.TryGetBrush(entity) == nullptr
            || !scene.IsEntityEffectivelyVisible(entity))
            continue;
        const auto fail = [&](std::string_view message)
        {
            const auto* id = world.TryGet<PersistentIdComponent>(entity);
            errors.push_back(std::format("entity {} trigger geometry: {}",
                id != nullptr ? PersistentEntityIdToString(id->Id) : std::format("{}:{}", entity.Index, entity.Generation),
                message));
        };
        if (world.HasComponent<Collider>(entity))
        {
            fail("the brush owns its sensor shape; remove the separately authored Collider");
            continue;
        }
        const auto* evaluated = scene.TryGetBrushPieces(entity, BrushEvaluationPolicy::Cook());
        if (evaluated == nullptr || evaluated->Status != BrushEvaluationStatus::Ok || evaluated->Pieces.size() != 1
            || evaluated->Meshes[evaluated->Pieces[0].MeshIndex].Role != BrushRole::Volume)
        {
            fail("use one closed Volume box; solid brushes, generated shells and multiple pieces are unsupported");
            continue;
        }
        const Transform3f frame = scene.ComposeWorldTransform(entity);
        const BodyTransform body{ frame.Position, frame.Rotation };
        if (!IsValidBodyTransform(body))
        {
            fail("the entity requires a finite position and unit rotation");
            continue;
        }
        const auto& piece = evaluated->Pieces[0];
        std::string error;
        const Transform3f scale{ Vec3d::Zero(), Quatf::Identity(), frame.Scale };
        const auto box = ExtractBrushBox(*piece.Mesh, scale.ToMat4() * piece.Placement.ToMat4(), error);
        if (!box)
        {
            fail(error);
            continue;
        }
        Collider collider;
        collider.Shape = CollisionShape::MakeBox(box->HalfExtents);
        collider.IsTrigger = true;
        collider.ShapePose = { box->Center, box->Rotation };
        sensors.push_back({ entity, collider });
    }
    return errors.size() == before;
}
