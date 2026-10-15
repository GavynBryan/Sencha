#include "AnimRigBinder.h"

#include <format>
#include <vector>

namespace
{
    // The first gameplay event `content` can fire, through a flow's sections and the
    // rows its slot sections play; null when it fires none.
    const AnimBoundEvent* GameplayEventOf(const AnimBoundRig& rig, int content, std::vector<bool>& visited)
    {
        if (content < 0 || static_cast<std::size_t>(content) >= rig.Contents.size()
            || visited[static_cast<std::size_t>(content)])
            return nullptr;
        visited[static_cast<std::size_t>(content)] = true;
        const AnimBoundContent& bound = rig.Contents[static_cast<std::size_t>(content)];
        for (const AnimBoundEvent& event : bound.Events)
            if (event.Scope == AuthoredScope::Gameplay)
                return &event;
        if (bound.Flow < 0)
            return nullptr;
        for (const AnimBoundFlowSection& section : rig.Flows[static_cast<std::size_t>(bound.Flow)].Sections)
        {
            for (const std::optional<AnimBoundEvent>* edge : { &section.Entered, &section.Exited })
                if (edge->has_value() && (*edge)->Scope == AuthoredScope::Gameplay)
                    return &**edge;
            if (const AnimBoundEvent* event = GameplayEventOf(rig, section.Content, visited))
                return event;
            if (section.Slot.IsValid())
                for (const AnimBoundSlotRow& row : rig.SlotRows)
                    if (row.Behavior == section.Slot)
                        if (const AnimBoundEvent* event = GameplayEventOf(rig, row.Content, visited))
                            return event;
        }
        return nullptr;
    }
}

void AnimRigBinder::ValidatePresentationLayers()
{
    for (std::size_t l = 0; l < Out.Layers.size(); ++l)
    {
        const AnimBoundLayer& layer = Out.Layers[l];
        if (!layer.Presentation)
            continue;
        const std::string field = std::format("$.data.layers[{}].presentation", l);
        if (layer.Selector >= 0)
            Error("anim.rig.presentation_layer", Out.RigPath, field,
                  std::format("Rig layer {} is presentation, so the requests this machine presents name what it "
                              "plays; it takes no selector.",
                              layer.NameText));

        // What presentation requests can make it play: its idle, and every behavior an intent names.
        for (const AnimBoundBehavior& behavior : Out.Behaviors)
        {
            if (behavior.Tag != layer.Idle && Out.FindIntent(behavior.Tag) == nullptr)
                continue;
            if (behavior.Policy.RootMotion)
                Error("anim.rig.presentation_layer", Out.RigPath, field,
                      std::format("Rig layer {} is presentation, but behavior '{}' has root motion.", layer.NameText,
                                  behavior.Name));
            const AnimBoundEvent* gameplay = nullptr;
            for (const std::optional<AnimBoundEvent>* edge : { &behavior.Entered, &behavior.Exited })
                if (gameplay == nullptr && edge->has_value() && (*edge)->Scope == AuthoredScope::Gameplay)
                    gameplay = &**edge;
            std::vector<bool> visited(Out.Contents.size(), false);
            for (const AnimBoundSlotRow& row : Out.SlotRows)
                if (gameplay == nullptr && row.Behavior == behavior.Tag)
                    gameplay = GameplayEventOf(Out, row.Content, visited);
            if (gameplay != nullptr)
                Error("anim.rig.presentation_layer", Out.RigPath, field,
                      std::format("Rig layer {} is presentation, but behavior '{}' fires gameplay event '{}'.",
                                  layer.NameText, behavior.Name, gameplay->BindingText));
        }
    }
}
