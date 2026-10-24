#include <logic/Trigger.h>

#include <ecs/World.h>
#include <logic/TriggerSequences.h>

VerbSequenceStore& TriggerSequencesOf(World& world)
{
    TriggerSequences* sequences = world.TryGetResource<TriggerSequences>();
    return sequences != nullptr ? sequences->Store : world.AddResource<TriggerSequences>().Store;
}

bool SetTriggerSequence(World& world, EntityId trigger, const JsonValue& authored)
{
    Trigger* component = world.IsRegistered<Trigger>() ? world.TryGet<Trigger>(trigger) : nullptr;
    if (component == nullptr)
        return false;
    component->Sequence = TriggerSequencesOf(world).Intern(authored);
    return true;
}

const JsonValue* TriggerSequenceSource(const World& world, EntityId trigger)
{
    const Trigger* component = world.IsRegistered<Trigger>() ? world.TryGet<Trigger>(trigger) : nullptr;
    const TriggerSequences* sequences = world.TryGetResource<TriggerSequences>();
    return component != nullptr && sequences != nullptr ? sequences->Store.Source(component->Sequence) : nullptr;
}
