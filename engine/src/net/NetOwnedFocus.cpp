#include <net/NetOwnedFocus.h>

#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include <algorithm>

FocusSourceId NetFocusSourceFor(EntityId entity)
{
    return FocusSourceId{ kNetFocusSourceBit | entity.Index };
}

void NetOwnedFocus::Update(const World& world, NetSessionRole role,
                           WorldPartitionRuntime& partition)
{
    Live_.clear();

    // A manifest is what makes a position mean a zone. Without one every call
    // below would mint a source carrying nothing, and the release pass would
    // then take them all away again next frame.
    if (role == NetSessionRole::Host && partition.HasManifest()
        && world.IsRegistered<NetOwner>())
    {
        const bool hasCapsules = world.IsRegistered<CharacterController>();

        world.ForEachComponent<NetOwner>(
            [&](EntityId entity, const NetOwner& owner)
            {
                // Peer zero is the authority itself. What it owns it is only
                // simulating, and it is already streaming around whatever it
                // locally drives.
                if (owner.Peer == kNetAuthorityPeer)
                    return;

                const WorldTransform* transform =
                    world.TryGet<WorldTransform>(entity);
                if (transform == nullptr)
                    return;

                const FocusSourceId source = NetFocusSourceFor(entity);
                partition.SetFocus(source, transform->Value.Position);
                if (hasCapsules)
                {
                    if (const CharacterController* shape =
                            world.TryGet<CharacterController>(entity))
                    {
                        partition.SetFocusCapsule(
                            source, shape->Radius, shape->Height);
                    }
                }
                Live_.push_back(source);
            });

        std::sort(Live_.begin(), Live_.end(),
                  [](FocusSourceId a, FocusSourceId b)
                  { return a.Value < b.Value; });
    }

    // Anything held a moment ago and not refreshed now is an entity nobody
    // drives any more -- destroyed, handed back, or belonging to a peer that
    // left. Releasing it does not tear anything down; what it alone was holding
    // enters linger like any zone somebody walked away from.
    for (const FocusSourceId held : Held_)
    {
        const bool live = std::binary_search(
            Live_.begin(), Live_.end(), held,
            [](FocusSourceId a, FocusSourceId b) { return a.Value < b.Value; });
        if (!live)
            partition.RemoveFocusSource(held);
    }

    Held_.swap(Live_);
}
