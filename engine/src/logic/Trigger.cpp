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
    VerbSequenceStore& store = TriggerSequencesOf(world);
    const VerbSequenceHandle previous = component->Sequence;
    component->Sequence = store.Intern(authored);
    store.Release(previous);
    return true;
}

const JsonValue* TriggerSequenceSource(const World& world, EntityId trigger)
{
    const Trigger* component = world.IsRegistered<Trigger>() ? world.TryGet<Trigger>(trigger) : nullptr;
    const TriggerSequences* sequences = world.TryGetResource<TriggerSequences>();
    return component != nullptr && sequences != nullptr ? sequences->Store.Source(component->Sequence) : nullptr;
}

void ComponentTraits<Trigger>::OnAdd(Trigger& trigger, World& world, EntityId)
{
    if (trigger.Sequence.IsValid())
        TriggerSequencesOf(world).Retain(trigger.Sequence);
}

void ComponentTraits<Trigger>::OnRemove(const Trigger& trigger, World& world, EntityId)
{
    if (TriggerSequences* sequences = world.TryGetResource<TriggerSequences>())
        sequences->Store.Release(trigger.Sequence);
}
