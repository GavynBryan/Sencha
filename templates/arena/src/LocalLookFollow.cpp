#include "LocalLookFollow.h"

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <participant/LocalControl.h>

bool FollowLocalLookControl(World& world, EntityId& tagged)
{
    const EntityId subject = LocalControlSubjectOf(world);
    if (subject == tagged)
        return false;

    if (world.IsRegistered<LocalLookControl>())
    {
        if (tagged.IsValid() && world.IsAlive(tagged)
            && world.HasComponent<LocalLookControl>(tagged))
        {
            world.RemoveComponent<LocalLookControl>(tagged);
        }
        if (subject.IsValid() && !world.HasComponent<LocalLookControl>(subject))
            world.AddComponent<LocalLookControl>(subject, {});
    }

    tagged = subject;
    return true;
}
