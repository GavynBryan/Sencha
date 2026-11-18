#include "HeldEditorAdapter.h"

#include "ComponentValueCommit.h"
#include "document/EntityLabel.h"
#include "properties/SettingProperties.h"

#include <ecs/Query.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kWorld = "The world";

    struct HolderChoice
    {
        std::string Label;
        PersistableRef Holder;
    };

    std::vector<HolderChoice> HolderChoices(const EditorComponentInspectorContext& context)
    {
        const World& world = std::as_const(context.Scene.GetRegistry()).Components;
        std::vector<HolderChoice> choices;
        Query<Read<PersistentIdComponent>> identified(world);
        identified.ForEachChunk([&](auto& view) {
            const auto ids = view.template Read<PersistentIdComponent>();
            for (std::uint32_t row = 0; row < view.Count(); ++row)
            {
                const EntityId entity = view.Entity(row);
                if (!ids[row].Id.IsValid() || IsHeldWithin(world, entity, context.Entity))
                    continue;
                choices.push_back({ std::format("{} ({})", EntityLabel(context.Document, entity),
                                                PersistentEntityIdToString(ids[row].Id)),
                                    PersistableRef{ .Entity = entity, .Target = ids[row].Id } });
            }
        });
        std::ranges::sort(choices, {}, &HolderChoice::Label);
        choices.insert(choices.begin(), HolderChoice{ std::string(kWorld), PersistableRef{} });
        return choices;
    }

    class HeldEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override { return ResolveComponentTypeId<Held>(); }
        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const Held* held = world.TryGet<Held>(context.Entity);
            if (held == nullptr)
                return {};
            std::vector<HolderChoice> choices = HolderChoices(context);
            auto current = std::ranges::find(choices, held->Holder.Target,
                                             [](const HolderChoice& choice) { return choice.Holder.Target; });
            if (current == choices.end())
                current = choices.insert(choices.end(), { std::format("Not in this document ({})",
                                                                      PersistentEntityIdToString(held->Holder.Target)),
                                                          held->Holder });
            std::vector<std::string_view> options;
            for (const HolderChoice& choice : choices)
                options.push_back(choice.Label);
            return { ChoiceProperty("holder", "Holder", options, static_cast<std::size_t>(current - choices.begin()),
                                    "What holds it: a chest's contents, a character's possessions, or the world's."),
                     NumberProperty("position", "Position", static_cast<double>(held->Position), 0.0, 65535.0, 1.0,
                                    "Its place among what its holder holds.") };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const Held* held = std::as_const(world).TryGet<Held>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            Held after = *held;
            if (key == "holder")
            {
                const std::vector<HolderChoice> choices = HolderChoices(context);
                const auto chosen = std::ranges::find(choices, value, &HolderChoice::Label);
                if (chosen == choices.end())
                    return PropertyEditResult::Refused;
                after.Holder = chosen->Holder;
            }
            else if (key == "position")
            {
                const std::optional<double> position = ParseNumber(value, 0.0, 65535.0);
                if (!position)
                    return PropertyEditResult::Refused;
                after.Position = static_cast<std::uint16_t>(*position);
            }
            else
                return PropertyEditResult::Gone;
            if (after.Holder.Target == held->Holder.Target && after.Position == held->Position)
                return PropertyEditResult::Unchanged;
            return CommitComponentValue(context, *held, after);
        }
    };
}

bool IsHeldWithin(const World& world, EntityId entity, EntityId holder)
{
    EntityId at = entity;
    for (int depth = 0; depth <= kHeldDeepestNesting && at.IsValid(); ++depth)
    {
        if (at == holder)
            return true;
        const Held* held = world.TryGet<Held>(at);
        at = held != nullptr ? ResolvePersistableRef(world, held->Holder) : EntityId{};
    }
    return false;
}

std::unique_ptr<IEditorComponentAdapter> MakeHeldEditorAdapter()
{
    return std::make_unique<HeldEditorAdapter>();
}
