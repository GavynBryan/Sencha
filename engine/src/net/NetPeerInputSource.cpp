#include <net/NetPeerInputSource.h>

#include <ecs/World.h>

InputActionSourceId NetSourceForPeer(World& world, PeerId peer)
{
    if (!peer.IsValid())
        return kLocalInputActionSource;

    NetPeerSources& mapping = world.HasResource<NetPeerSources>()
        ? world.GetResource<NetPeerSources>()
        : world.AddResource<NetPeerSources>();

    const auto it = mapping.Sources.find(peer.Value);
    if (it != mapping.Sources.end())
        return it->second;

    InputActionSourceIds& ids = world.HasResource<InputActionSourceIds>()
        ? world.GetResource<InputActionSourceIds>()
        : world.AddResource<InputActionSourceIds>();

    const InputActionSourceId allocated = ids.Allocate();
    mapping.Sources.emplace(peer.Value, allocated);
    return allocated;
}

InputActionSourceId NetFindSourceForPeer(const World& world, PeerId peer)
{
    if (!peer.IsValid())
        return kLocalInputActionSource;

    const NetPeerSources* mapping = world.TryGetResource<NetPeerSources>();
    if (mapping == nullptr)
        return kLocalInputActionSource;

    const auto it = mapping->Sources.find(peer.Value);
    return it == mapping->Sources.end() ? kLocalInputActionSource : it->second;
}

void NetReleasePeerSource(World& world, PeerId peer)
{
    if (!peer.IsValid())
        return;

    const InputActionSourceId source = NetFindSourceForPeer(world, peer);
    if (source != kLocalInputActionSource)
    {
        if (InputActionSourceTable* sources =
                world.TryGetResource<InputActionSourceTable>())
        {
            sources->Close(source);
        }
    }

    if (NetPeerSources* mapping = world.TryGetResource<NetPeerSources>())
        mapping->Sources.erase(peer.Value);
}
