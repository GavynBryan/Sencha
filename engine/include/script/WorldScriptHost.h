#pragma once

#include <script/ScriptRuntime.h>
#include <script/ScriptVm.h>

class World;
class CommandBuffer;
class GameplayTagRegistry;

//=============================================================================
// WorldScriptHost (docs/plans/t-language-v1-spec.md, sections C, D)
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
    // seeding randomness.
    void Begin(std::uint32_t linkedModuleIndex, EntityId subject, std::uint64_t tick);

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
    World& W;
    CommandBuffer& Commands;
    const ScriptRuntime& Runtime;
    GameplayTagRegistry* TagRegistry = nullptr;

    const ScriptLinkedModule* Linked = nullptr;
    EntityId Subject{};
    std::uint64_t Tick = 0;
};
