#include <world/WorldComponentRegistration.h>

#include <world/relocation/RelocationRules.h>

namespace
{
    // A child's local transform is in its parent's space.
    void MapRootPose(void*, World& world, EntityId entity, const RelocationFrame& frame)
    {
        if (world.HasComponent<Parent>(entity))
            return;
        if (LocalTransform* local = world.TryGet<LocalTransform>(entity))
            local->Value = frame.MapPose(local->Value);
    }

    void MapWorldPose(WorldTransform& placed, const RelocationFrame& frame)
    {
        placed.Value = frame.MapPose(placed.Value);
    }

    void CarryHistory(WorldTransformHistory& history, const RelocationFrame& frame)
    {
        CarryWorldTransformHistory(history, frame.Mapping, frame.TickFraction);
    }
}

void RegisterWorldRelocation(World& world)
{
    SetRelocationRule<LocalTransform>(world, RelocationRuleKind::Maps, &MapRootPose);
    SetRelocationRule<WorldTransform>(world, RelocationRuleKind::Maps,
                                      &RelocateValue<WorldTransform, &MapWorldPose>);
    SetRelocationRule<WorldTransformHistory>(world, RelocationRuleKind::Maps,
                                             &RelocateValue<WorldTransformHistory, &CarryHistory>);
}
