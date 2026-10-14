#pragma once

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityLocals.h>
#include <abilities/AbilitySystem.h>
#include <abilities/AbilityTrace.h>
#include <abilities/ActiveAbilities.h>
#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <input/InputActionState.h>
#include <time/ActorTime.h>
#include <time/FrameClock.h>

#include <effects/EffectId.h>

#include <cstdint>
#include <vector>

class AbilityRegistry;
class AttributeRegistry;
class GameplayTagRegistry;

// One actor's components, fetched again at every use: an operation an ability
// invokes may change the actor's archetype, and a pointer held across it would
// dangle.
struct AbilityActor
{
    World* Entities = nullptr;
    EntityId Entity;
    InputActionView Input;
    InputActionSourceId Source = kLocalInputActionSource;

    [[nodiscard]] ActiveAbilities& Active() const { return *Entities->TryGet<ActiveAbilities>(Entity); }
    [[nodiscard]] AbilityRecord& Record(std::size_t slot) const { return Active().Records[slot]; }
    [[nodiscard]] AbilityRecordLocals& Locals(std::size_t slot) const
    {
        return Entities->TryGet<AbilityLocals>(Entity)->Records[slot];
    }
    [[nodiscard]] const AbilityGrants* Grants() const
    {
        return static_cast<const World&>(*Entities).TryGet<AbilityGrants>(Entity);
    }
    [[nodiscard]] GameplayTagContainer* Tags() const { return Entities->TryGet<GameplayTagContainer>(Entity); }
    [[nodiscard]] bool Alive() const { return Entities->IsAlive(Entity); }
};

// What one pass reuses from the last, as a World resource.
struct AbilityPassBuffers
{
    std::vector<AuthoredValue> Inputs;
    AuthoredValue Answer;
    std::vector<EffectId> Costs;
};

struct AbilityCommitResult
{
    bool Paid = false;
    // The cost group whose check failed, when one did.
    std::uint8_t Group = 0;
};

// One fixed tick's ability work, over every actor. Owner-thread only; the operations
// it invokes may create and destroy entities, so it is never run inside a query.
class AbilityPass
{
public:
    AbilityPass(World& world, AbilityPassBuffers& buffers, const AbilityHost& host, const FixedSimTime& now,
                std::uint32_t stepBudget, std::uint32_t lingerTicks);

    [[nodiscard]] std::uint64_t Tick() const { return Now.TickIndex; }

    // Admission: everything a start needs, checked without changing anything.
    [[nodiscard]] AbilityRefusal CanActivate(const AbilityActor& actor, const AbilityGrant& grant,
                                             std::uint16_t& failing);
    // Admits and starts, then runs the new record until it waits or ends. The slot
    // started, or -1 with the refusal traced.
    int TryStart(const AbilityActor& actor, const AbilityGrant& grant, AbilityActivationKey key,
                 std::uint8_t triggerAction, EntityId instigator);

    void Run(const AbilityActor& actor, std::size_t slot);
    // Every hold of the record's phase that can complete has, and it holds one.
    [[nodiscard]] bool HoldsCompleted(const AbilityActor& actor, std::size_t slot);
    void End(const AbilityActor& actor, std::size_t slot, AbilityEndReason reason, std::uint16_t element = 0xFFFF);

    void Sweep(const AbilityActor& actor);
    void Advance(const AbilityActor& actor);
    void Linger(const AbilityActor& actor);

    [[nodiscard]] const AbilityDefinition* DefinitionOf(const AbilityRecord& record) const;
    void Trace(AbilityTraceRecord record);

private:
    // Conditions and values (AbilityConditions.cpp).
    [[nodiscard]] bool Holds(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                             std::uint16_t condition);
    [[nodiscard]] bool AllHold(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                               const std::vector<std::uint16_t>& conditions, std::uint16_t& failing);
    void Evaluate(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                  const AbilityValue& value, AuthoredValue& out);
    [[nodiscard]] double Number(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                                const AbilityValue& value);
    [[nodiscard]] EntityId EntityOf(const AbilityActor& actor, std::size_t slot, const AbilityValue& value) const;
    [[nodiscard]] std::uint64_t PhaseTicks(const AbilityActor& actor, const AbilityRecord& record,
                                           const AbilityDefinition& definition) const;
    [[nodiscard]] std::uint64_t ClockOf(const AbilityActor& actor, AbilityTimeDomain domain) const;

    // Costs (AbilityCommit.cpp).
    [[nodiscard]] AbilityCommitResult CheckCosts(const AbilityActor& actor, const AbilityDefinition& definition,
                                                 std::uint8_t groups, bool cooldown);
    AbilityCommitResult Commit(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                               std::uint8_t groups);
    void StartCooldown(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition);

    // Execution (AbilityPass.cpp).
    void EnterPhase(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                    std::uint8_t phase);
    void LeavePhase(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                    bool runExit);
    void ReleaseLeases(const AbilityActor& actor, std::size_t slot);
    bool RunOps(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                const std::vector<std::uint16_t>& ops);
    bool RunOp(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
               std::uint16_t index);
    bool Operate(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                 std::uint16_t index);
    void Pulse(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition);
    // Where a cue happened, as its `at` value says.
    struct CuePlace
    {
        Vec3d Point = Vec3d::Zero();
        Vec3d Normal = Vec3d::Zero();
        EntityId Target;
    };
    [[nodiscard]] static CuePlace PlaceOf(const AuthoredValue& at);
    [[nodiscard]] bool Live(const AbilityActor& actor, std::size_t slot) const;
    [[nodiscard]] LeaseOwner OwnerOf(const AbilityActor& actor, std::size_t slot, std::uint8_t op) const;
    void Write(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition, std::uint8_t local,
               const AuthoredValue& answer, bool answered);
    int ClaimSlot(const AbilityActor& actor);

    World& Entities;
    AbilityHost Host;
    FixedSimTime Now;
    std::uint32_t StepBudget;
    std::uint32_t LingerTicks;
    const AbilityRegistry* Abilities = nullptr;
    const GameplayTagRegistry* TagRegistry = nullptr;
    const AttributeRegistry* Attributes = nullptr;
    AbilityTrace* History = nullptr;

    // The op being run with `targets`, and its current element.
    EntityId Target;
    // Borrowed from the World's AbilityPassBuffers, so a warm pass allocates nothing.
    std::vector<AuthoredValue>& Inputs;
    AuthoredValue& Answer;
    std::vector<EffectId>& Costs;
};
