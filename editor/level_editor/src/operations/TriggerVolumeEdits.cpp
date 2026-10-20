#include "TriggerVolumeEdits.h"

#include "brush/BrushBox.h"
#include "document/EditorDocument.h"
#include "document/EntityChangeSet.h"
#include "document/SpaceComponents.h"

#include <logic/TriggerPrograms.h>
#include <physics/components/Collider.h>

namespace
{
bool EmptyProgram(const JsonValue* source)
{
    if (source == nullptr || !source->IsObject() || source->NumberOr("version", 0) != 1)
        return false;
    for (const auto& [key, value] : source->AsObject())
    {
        if (key == "version")
            continue;
        if ((key != "commands" && key != "conditions" && key != "require_components")
            || !value.IsArray() || !value.AsArray().empty())
            return false;
    }
    return true;
}
}

std::string TriggerVolumeConversionProblem(
    const EditorDocument& document, std::span<const EntityId> entities, bool toTrigger)
{
    if (entities.empty())
        return "Select a brush in Object mode.";
    const auto& scene = document.GetScene();
    const auto& world = scene.GetRegistry().Components;
    for (const auto entity : entities)
    {
        if (!world.IsAlive(entity) || scene.TryGetBrush(entity) == nullptr)
            return "Conversion requires an authored brush.";
        if (document.IsSceneInstanceMember(entity))
            return "Edit the source scene or explicitly break the scene instance before converting.";
        if (scene.IsEntityEffectivelyLocked(entity))
            return "Unlock the brush before converting.";
        if (world.HasComponent<Space>(entity) || world.HasComponent<Shell>(entity)
            || world.HasComponent<Opening>(entity) || world.HasComponent<GeneratedWall>(entity))
            return "Space, shell, opening and generated-wall geometry has another owner; create a separate trigger volume.";
        if (world.HasComponent<Collider>(entity))
            return "Remove the separately authored Collider first; the trigger brush must own its sensor geometry.";
        const auto* evaluated = scene.TryGetBrushPieces(entity, BrushEvaluationPolicy::Cook());
        if (evaluated == nullptr || evaluated->Status != BrushEvaluationStatus::Ok || evaluated->Pieces.size() != 1)
            return "Conversion supports one closed box. Bake or simplify the modifier stack first.";
        std::string error;
        const auto& piece = evaluated->Pieces[0];
        const auto frame = scene.ComposeWorldTransform(entity);
        const Transform3f scale{ {}, Quatf::Identity(), frame.Scale };
        if (!ExtractBrushBox(*piece.Mesh, scale.ToMat4() * piece.Placement.ToMat4(), error))
            return error;
        if (!toTrigger)
        {
            if (!world.HasComponent<Trigger>(entity) || !world.HasComponent<BrushVolume>(entity))
                return "Select a Trigger Volume to convert back to a solid brush.";
            if (!EmptyProgram(TriggerProgramSource(world, entity)))
                return "The trigger contains commands, conditions or other authored data. Duplicate or preserve that configuration before removing it.";
            const auto snapshot = document.CaptureEntity(entity);
            for (const auto& [key, value] : snapshot.Components.AsObject())
                if (key != "trigger" && key != "volume" && key != "brush" && key != "Transform"
                    && key != "persistent_id" && key != "name")
                    return "This volume carries other components. Their geometry ownership must be resolved before making it solid.";
        }
    }
    return {};
}

EditorOperationOutcome ConvertTriggerVolumes(
    EditorDocument& document, EntityChangeRecorder& recorder, std::span<const EntityId> entities, bool toTrigger)
{
    const auto problem = TriggerVolumeConversionProblem(document, entities, toTrigger);
    if (!problem.empty())
        return EditorOperationOutcome::Refused("trigger.conversion", problem);
    auto& world = document.GetScene().GetRegistry().Components;
    for (const auto entity : entities)
    {
        recorder.Touch(entity);
        if (toTrigger)
        {
            if (!world.HasComponent<BrushVolume>(entity))
                world.AddComponent(entity, BrushVolume{});
            if (!world.HasComponent<Trigger>(entity))
                world.AddComponent(entity, Trigger{});
        }
        else
        {
            world.RemoveComponent<Trigger>(entity);
            world.RemoveComponent<BrushVolume>(entity);
        }
    }
    document.MarkDirty();
    return EditorOperationOutcome::Ok({});
}
