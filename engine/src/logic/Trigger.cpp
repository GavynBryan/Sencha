#include <logic/Trigger.h>
#include <logic/TriggerPrograms.h>

void ComponentTraits<Trigger>::OnAdd(Trigger &trigger, World &world, EntityId entity)
{
    trigger.Program = TriggerProgramsOf(world).Attach(entity, trigger.Program);
}
void ComponentTraits<Trigger>::OnRemove(const Trigger &, World &world, EntityId entity)
{
    TriggerProgramsOf(world).Detach(entity);
}
