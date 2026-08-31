#pragma once

#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagId.h>
#include <movement/components/CharacterMovement.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

class GameplayTagRegistry;

struct LocomotionModeEntry
{
    LocomotionModeId Id;
    std::string Name;
    GameplayTagId ActiveTag;
    GameplayTagId RequestTag;

    // CanEnter is evaluated before the current session is touched. EnterSession
    // may therefore assume its required candidate data exists. This is the seam
    // that lets a mode such as Cling capture a surface before requesting entry,
    // then atomically turn that candidate into the live session.
    std::function<bool(const World&, EntityId)> CanEnter;
    std::function<void(World&, EntityId)> EnterSession;
    std::function<void(World&, EntityId)> ExitSession;
    std::function<void(World&, EntityId)> DiscardCandidate;

    [[nodiscard]] bool HasSession() const
    {
        return static_cast<bool>(EnterSession) && static_cast<bool>(ExitSession);
    }
};

//=============================================================================
// LocomotionModeRegistry
//
// The open set of locomotion modes. A mode is a registration, not a type: an
// id, a name, its active and request tags, and optionally the session
// component it owns. A game module adds a mode without editing any engine
// system, which is why nothing here switches on a fixed set.
//
// Ids are registration order starting at 1, so they are stable within a run
// and meaningless across runs. Modes are never unregistered.
//=============================================================================
class LocomotionModeRegistry
{
public:
    explicit LocomotionModeRegistry(GameplayTagRegistry& tags);

    [[nodiscard]] LocomotionModeId RegisterFree(std::string name = "movement.mode.free");

    template<typename Session>
    [[nodiscard]] LocomotionModeId Register(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<Session>);
        return RegisterImpl(
            std::move(name),
            {},
            [](World& world, EntityId entity)
            {
                if (!world.HasComponent<Session>(entity))
                    world.AddComponent<Session>(entity, Session{});
            },
            [](World& world, EntityId entity)
            {
                if (world.HasComponent<Session>(entity))
                    world.RemoveComponent<Session>(entity);
            },
            {});
    }

    template<typename Session, typename Candidate, typename Convert>
    [[nodiscard]] LocomotionModeId RegisterWithCandidate(std::string name, Convert convert)
    {
        static_assert(std::is_trivially_copyable_v<Session>);
        static_assert(std::is_trivially_copyable_v<Candidate>);

        return RegisterImpl(
            std::move(name),
            [](const World& world, EntityId entity)
            {
                return world.TryGet<Candidate>(entity) != nullptr;
            },
            [convert = std::move(convert)](World& world, EntityId entity)
            {
                const Candidate* candidate = std::as_const(world).TryGet<Candidate>(entity);
                if (candidate == nullptr)
                    return;
                const Session session = convert(*candidate);
                if (!world.HasComponent<Session>(entity))
                    world.AddComponent<Session>(entity, session);
                world.RemoveComponent<Candidate>(entity);
            },
            [](World& world, EntityId entity)
            {
                if (world.HasComponent<Session>(entity))
                    world.RemoveComponent<Session>(entity);
            },
            [](World& world, EntityId entity)
            {
                if (world.HasComponent<Candidate>(entity))
                    world.RemoveComponent<Candidate>(entity);
            });
    }

    [[nodiscard]] const LocomotionModeEntry* Find(LocomotionModeId id) const;
    [[nodiscard]] const LocomotionModeEntry* Find(std::string_view name) const;
    [[nodiscard]] const LocomotionModeEntry* FindByRequestTag(GameplayTagId tag) const;
    [[nodiscard]] std::span<const LocomotionModeEntry> Entries() const { return Modes; }
    [[nodiscard]] LocomotionModeId FreeMode() const { return Free; }
    [[nodiscard]] uint64_t Generation() const { return RegistryGeneration; }

private:
    [[nodiscard]] LocomotionModeId RegisterImpl(
        std::string name,
        std::function<bool(const World&, EntityId)> canEnter,
        std::function<void(World&, EntityId)> enterSession,
        std::function<void(World&, EntityId)> exitSession,
        std::function<void(World&, EntityId)> discardCandidate);

    GameplayTagRegistry& Tags;
    std::vector<LocomotionModeEntry> Modes;
    LocomotionModeId Free;
    uint64_t RegistryGeneration = 0;
};

[[nodiscard]] bool RequestLocomotionMode(World& world,
                                         EntityId entity,
                                         LocomotionModeId target,
                                         ModeRequestClass requestClass);
