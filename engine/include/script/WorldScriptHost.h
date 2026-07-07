#pragma once

#include <ecs/ComponentId.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptVm.h>

#include <cstdint>
#include <map>
#include <string_view>
#include <utility>

class World;
class CommandBuffer;
class GameplayTagRegistry;

// Outcome an ability callback requested through ctx.cancel()/ctx.finish(). The
// ability bridge reads it after each invocation.
enum class ScriptAbilityOutcome
{
    None,
    Finish,
    Cancel,
};

// The closed set of host operations WorldScriptHost implements. An import name
// resolves to one of these once at link (ResolveScriptHostOp); the host then
// dispatches on the id. Unknown covers names whose owning system is not present
// in v1 (physics, movement), which trap as an argument error at call time.
enum class ScriptHostOp : std::int32_t
{
    Unknown = -1,
    AbilityFinish,
    AbilityCancel,
    RandomF32,
    RandomRange,
    CommandsDestroy,
    CommandsRemove,
    CommandsAdd,
    CueFire,
    CueFireAt,
};

// Maps a host import's dotted name to its op id. Runs at link, not per call.
[[nodiscard]] ScriptHostOp ResolveScriptHostOp(std::string_view name);

//=============================================================================
// WorldScriptHost
//
// The ScriptHostCallHandler implementation over real engine seams: component
// field access through World::GetComponentRaw plus the linked field binds
// (converting stored f32/i32/... to and from the VM's 64-bit registers), tag
// grant/revoke on GameplayTagContainer, structural changes through a
// CommandBuffer, and deterministic randomness.
//
// One host serves a whole tick: Begin() sets the current linked module and
// subject entity before each VM invocation. Everything mutating either bumps
// a component column version (immediate, in place) or enqueues onto the
// CommandBuffer (structural, deferred to the phase boundary).
//=============================================================================
class WorldScriptHost final : public ScriptHostCallHandler
{
public:
    WorldScriptHost(World& world, CommandBuffer& commands, const ScriptRuntime& runtime);

    // Binds the host to one invocation: which linked module the callback
    // belongs to, the subject entity (owner/entity), and the fixed tick for
    // seeding randomness. Resets the pending ability outcome.
    void Begin(std::uint32_t linkedModuleIndex, EntityId subject, std::uint64_t tick);

    // The outcome the just-invoked ability callback requested, if any.
    [[nodiscard]] ScriptAbilityOutcome Outcome() const { return PendingOutcome; }

    ScriptTrapCode HostCall(const ScriptModule& module, std::uint32_t importIndex,
                            std::span<std::uint64_t> window, std::uint32_t argCount) override;
    ScriptTrapCode ComponentLoad(const ScriptModule& module, std::uint64_t entityBits,
                                 std::uint32_t fieldBind, std::uint32_t elementIndex,
                                 std::span<std::uint64_t> out) override;
    ScriptTrapCode ComponentStore(const ScriptModule& module, std::uint64_t entityBits,
                                  std::uint32_t fieldBind, std::uint32_t elementIndex,
                                  std::span<const std::uint64_t> in) override;
    ScriptTrapCode ComponentHas(const ScriptModule& module, std::uint64_t entityBits,
                                std::uint32_t componentBind, bool& out) override;
    ScriptTrapCode TagHas(const ScriptModule& module, std::uint64_t entityBits,
                          std::uint32_t tagBind, bool hierarchical, bool& out) override;
    ScriptTrapCode TagAdd(const ScriptModule& module, std::uint64_t entityBits,
                          std::uint32_t tagBind) override;
    ScriptTrapCode TagRemove(const ScriptModule& module, std::uint64_t entityBits,
                             std::uint32_t tagBind) override;

private:
    // The net effect a structural command will have at flush, per (entity, component).
    // Script content must never enqueue an add of a present component or a remove of
    // an absent one: the CommandBuffer flush asserts on those, which would abort the
    // whole player. Mirroring the buffer's pending effect here lets the add/remove
    // ops reject an illegal change as a deterministic trap at enqueue time, robust
    // against multiple commands touching the same (entity, component) in one Step.
    // Lives for the Step (the host is constructed per Step alongside the buffer).
    bool WillHaveComponent(std::uint64_t entityBits, ComponentId component);
    void SetPendingPresence(std::uint64_t entityBits, ComponentId component, bool present);

    World& W;
    CommandBuffer& Commands;
    const ScriptRuntime& Runtime;
    GameplayTagRegistry* TagRegistry = nullptr;

    const ScriptLinkedModule* Linked = nullptr;
    EntityId Subject{};
    std::uint64_t Tick = 0;
    ScriptAbilityOutcome PendingOutcome = ScriptAbilityOutcome::None;
    std::map<std::pair<std::uint64_t, ComponentId>, bool> PendingPresence;
};
