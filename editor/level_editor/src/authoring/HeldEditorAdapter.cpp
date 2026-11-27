#include "HeldEditorAdapter.h"

#include "ComponentValueCommit.h"
#include "properties/SettingProperties.h"

#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kHolder = "holder";

    // A holder is another entity of the same document, never one it holds:
    // what it holds is parked with the zone it is in.
    EntityReferenceFilter Holders(const EditorComponentInspectorContext& context)
    {
        return EntityReferenceFilter{ .Within = &context.Document,
                                      .Admits = [held = context.Entity](const EntityReferenceSubject& subject) {
                                          return !IsHeldWithin(subject.Components, subject.Entity, held);
                                      } };
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
            return { EntityReferenceProperty(std::string(kHolder), "Holder",
                                             held->Holder.Target.IsValid()
                                                 ? PersistentEntityIdToString(held->Holder.Target)
                                                 : std::string{},
                                             "The world",
                                             "What holds it: a chest's contents, a character's possessions, or the "
                                             "world's."),
                     NumberProperty("position", "Position", static_cast<double>(held->Position), 0.0, 65535.0, 1.0,
                                    "Its place among what its holder holds.") };
        }

        std::optional<EntityReferenceFilter> ReferenceFilter(const EditorComponentInspectorContext& context,
                                                             std::string_view key) const override
        {
            return key == kHolder ? std::optional(Holders(context)) : std::nullopt;
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const Held* held = std::as_const(world).TryGet<Held>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            Held after = *held;
            if (key == kHolder)
            {
                after.Holder = {};
                if (!value.empty())
                {
                    const std::optional<PersistentEntityId> target = PersistentEntityIdFromString(value);
                    const std::optional<EntityReferenceSubject> holder =
                        target.has_value() ? AdmittedEntityReference(context.World, Holders(context), *target)
                                           : std::nullopt;
                    if (!holder.has_value())
                        return PropertyEditResult::Refused;
                    after.Holder = PersistableRef{ .Entity = holder->Entity, .Target = *target };
                }
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
