#include <movement/LocomotionMode.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
    std::string RequestTagName(std::string_view modeName)
    {
        constexpr std::string_view prefix = "movement.mode.";
        if (modeName.starts_with(prefix))
            return "movement.request." + std::string(modeName.substr(prefix.size()));
        return "movement.request." + std::string(modeName);
    }
}

LocomotionModeRegistry::LocomotionModeRegistry(GameplayTagRegistry& tags)
    : Tags(tags)
{
}

LocomotionModeId LocomotionModeRegistry::RegisterFree(std::string name)
{
    const LocomotionModeId id = RegisterImpl(std::move(name), {}, {}, {}, {});
    if (!Free.IsValid())
        Free = id;
    return id;
}

LocomotionModeId LocomotionModeRegistry::RegisterImpl(
    std::string name,
    std::function<bool(const World&, EntityId)> canEnter,
    std::function<void(World&, EntityId)> enterSession,
    std::function<void(World&, EntityId)> exitSession,
    std::function<void(World&, EntityId)> discardCandidate)
{
    if (const LocomotionModeEntry* existing = Find(name))
        return existing->Id;

    const auto activeTag = Tags.RegisterTag(name);
    const auto requestTag = Tags.RegisterTag(RequestTagName(name));
    if (!activeTag.has_value() || !requestTag.has_value())
        return {};

    const LocomotionModeId id{ static_cast<uint32_t>(Modes.size() + 1) };
    Modes.push_back(LocomotionModeEntry{
        .Id = id,
        .Name = std::move(name),
        .ActiveTag = *activeTag,
        .RequestTag = *requestTag,
        .CanEnter = std::move(canEnter),
        .EnterSession = std::move(enterSession),
        .ExitSession = std::move(exitSession),
        .DiscardCandidate = std::move(discardCandidate),
    });
    ++RegistryGeneration;
    return id;
}

const LocomotionModeEntry* LocomotionModeRegistry::Find(LocomotionModeId id) const
{
    if (!id.IsValid() || id.Value > Modes.size())
        return nullptr;
    return &Modes[id.Value - 1];
}

const LocomotionModeEntry* LocomotionModeRegistry::Find(std::string_view name) const
{
    const auto it = std::find_if(Modes.begin(), Modes.end(), [name](const LocomotionModeEntry& entry)
    {
        return entry.Name == name;
    });
    return it == Modes.end() ? nullptr : &*it;
}

const LocomotionModeEntry* LocomotionModeRegistry::FindByRequestTag(GameplayTagId tag) const
{
    const auto it = std::find_if(Modes.begin(), Modes.end(), [tag](const LocomotionModeEntry& entry)
    {
        return entry.RequestTag == tag;
    });
    return it == Modes.end() ? nullptr : &*it;
}

bool RequestLocomotionMode(World& world,
                           EntityId entity,
                           LocomotionModeId target,
                           ModeRequestClass requestClass)
{
    if (!target.IsValid() || !world.IsRegistered<ModeTransitionRequest>())
        return false;

    const LocomotionModeRegistry* modes = std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (modes == nullptr || modes->Find(target) == nullptr)
        return false;

    ModeTransitionRequest* request = world.TryGet<ModeTransitionRequest>(entity);
    if (request == nullptr)
        return false;

    if (request->Pending)
    {
        const auto incoming = static_cast<uint8_t>(requestClass);
        const auto current = static_cast<uint8_t>(request->Class);
        if (incoming <= current)
            return false;
    }

    request->Target = target;
    request->Class = requestClass;
    request->Pending = true;
    return true;
}
