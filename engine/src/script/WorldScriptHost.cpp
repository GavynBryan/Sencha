#include <script/WorldScriptHost.h>

#include <core/random/DeterministicRandom.h>
#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <bit>
#include <cstring>
#include <utility>

namespace
{
    // Storage scalar <-> 64-bit register conversions (spec D.2). Floats live
    // as f64 in registers; f32 fields widen on load and round on store.
    std::uint64_t LoadScalar(const std::byte* at, ScriptScalarKind scalar)
    {
        switch (scalar)
        {
        case ScriptScalarKind::Bool:
        {
            std::uint8_t v = 0;
            std::memcpy(&v, at, 1);
            return v != 0 ? 1u : 0u;
        }
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        case ScriptScalarKind::TagId:
        {
            std::uint32_t v = 0;
            std::memcpy(&v, at, 4);
            return v;
        }
        case ScriptScalarKind::Int64:
        case ScriptScalarKind::Entity:
        {
            std::uint64_t v = 0;
            std::memcpy(&v, at, 8);
            return v;
        }
        case ScriptScalarKind::Float:
        {
            float v = 0.0f;
            std::memcpy(&v, at, 4);
            return std::bit_cast<std::uint64_t>(static_cast<double>(v));
        }
        case ScriptScalarKind::Double:
        case ScriptScalarKind::Color3:
        {
            std::uint64_t v = 0;
            std::memcpy(&v, at, 8);
            return v;
        }
        }
        return 0;
    }

    void StoreScalar(std::byte* at, ScriptScalarKind scalar, std::uint64_t reg)
    {
        switch (scalar)
        {
        case ScriptScalarKind::Bool:
        {
            const std::uint8_t v = reg != 0 ? 1u : 0u;
            std::memcpy(at, &v, 1);
            return;
        }
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        case ScriptScalarKind::TagId:
        {
            const std::uint32_t v = static_cast<std::uint32_t>(reg);
            std::memcpy(at, &v, 4);
            return;
        }
        case ScriptScalarKind::Int64:
        case ScriptScalarKind::Entity:
            std::memcpy(at, &reg, 8);
            return;
        case ScriptScalarKind::Float:
        {
            const float v = static_cast<float>(std::bit_cast<double>(reg));
            std::memcpy(at, &v, 4);
            return;
        }
        case ScriptScalarKind::Double:
        case ScriptScalarKind::Color3:
            std::memcpy(at, &reg, 8);
            return;
        }
    }
}

WorldScriptHost::WorldScriptHost(World& world, CommandBuffer& commands,
                                 const ScriptRuntime& runtime)
    : W(world)
    , Commands(commands)
    , Runtime(runtime)
    , TagRegistry(world.TryGetResource<GameplayTagRegistry>())
{
}

void WorldScriptHost::Begin(std::uint32_t linkedModuleIndex, EntityId subject, std::uint64_t tick)
{
    Linked = linkedModuleIndex < Runtime.Modules.size()
                 ? &Runtime.Modules[linkedModuleIndex]
                 : nullptr;
    Subject = subject;
    Tick = tick;
}

ScriptTrapCode WorldScriptHost::ComponentLoad(const ScriptModule&, std::uint64_t entityBits,
                                              std::uint32_t fieldBind, std::uint32_t elementIndex,
                                              std::span<std::uint64_t> out)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    const ResolvedFieldBind& bind = Linked->Fields[fieldBind];
    const void* base = W.GetComponentRaw(entity, bind.Component);
    if (base == nullptr)
    {
        return ScriptTrapCode::NoComponent;
    }
    const auto scalar = static_cast<ScriptScalarKind>(bind.Scalar);
    const std::size_t scalarSize = ScriptScalarSize(bind.Scalar);
    const std::byte* start = static_cast<const std::byte*>(base) + bind.Offset
                             + static_cast<std::size_t>(elementIndex) * bind.SlotCount * scalarSize;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = LoadScalar(start + i * scalarSize, scalar);
    }
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::ComponentStore(const ScriptModule&, std::uint64_t entityBits,
                                               std::uint32_t fieldBind, std::uint32_t elementIndex,
                                               std::span<const std::uint64_t> in)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    const ResolvedFieldBind& bind = Linked->Fields[fieldBind];
    // Non-const GetComponentRaw bumps the column version, so Changed<T> and
    // serialization observe the write.
    void* base = W.GetComponentRaw(entity, bind.Component);
    if (base == nullptr)
    {
        return ScriptTrapCode::NoComponent;
    }
    const auto scalar = static_cast<ScriptScalarKind>(bind.Scalar);
    const std::size_t scalarSize = ScriptScalarSize(bind.Scalar);
    std::byte* start = static_cast<std::byte*>(base) + bind.Offset
                       + static_cast<std::size_t>(elementIndex) * bind.SlotCount * scalarSize;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        StoreScalar(start + i * scalarSize, scalar, in[i]);
    }
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::ComponentHas(const ScriptModule&, std::uint64_t entityBits,
                                             std::uint32_t componentBind, bool& out)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    out = W.HasComponent(entity, Linked->Components[componentBind]);
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::TagHas(const ScriptModule&, std::uint64_t entityBits,
                                       std::uint32_t tagBind, bool hierarchical, bool& out)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    const GameplayTagContainer* tags = std::as_const(W).TryGet<GameplayTagContainer>(entity);
    if (tags == nullptr)
    {
        out = false;
        return ScriptTrapCode::None;
    }
    const GameplayTagId tag = Linked->Tags[tagBind];
    out = (hierarchical && TagRegistry != nullptr)
              ? tags->HasDescendantOf(*TagRegistry, tag)
              : tags->HasExact(tag);
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::TagAdd(const ScriptModule&, std::uint64_t entityBits,
                                       std::uint32_t tagBind)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    // Mutate through the non-const accessor so the column version bumps.
    GameplayTagContainer* tags = W.TryGet<GameplayTagContainer>(entity);
    if (tags == nullptr)
    {
        return ScriptTrapCode::TagCapacity;
    }
    tags->Grant(Linked->Tags[tagBind]);
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::TagRemove(const ScriptModule&, std::uint64_t entityBits,
                                          std::uint32_t tagBind)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    if (GameplayTagContainer* tags = W.TryGet<GameplayTagContainer>(entity))
    {
        tags->Revoke(Linked->Tags[tagBind]);
    }
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::HostCall(const ScriptModule& module, std::uint32_t importIndex,
                                         std::span<std::uint64_t> window, std::uint32_t argCount)
{
    const std::string_view name = module.GetString(module.HostImports[importIndex].Name);

    // Deterministic randomness (spec D.6): a per-callback stream seeded from
    // the world seed, the subject entity, and the tick.
    if (name.starts_with("random."))
    {
        const std::uint64_t streamId =
            PackScriptEntity(Subject) ^ (static_cast<std::uint64_t>(importIndex) << 40);
        DeterministicRandom rng =
            DeterministicRandom::FromInputs(Runtime.WorldSeed, streamId, Tick);
        if (name == "random.f32")
        {
            window[0] = std::bit_cast<std::uint64_t>(static_cast<double>(rng.NextFloat01()));
            return ScriptTrapCode::None;
        }
        if (name == "random.range")
        {
            const int32_t lo = static_cast<int32_t>(window[0]);
            const int32_t hi = static_cast<int32_t>(window[1]);
            if (lo > hi)
            {
                return ScriptTrapCode::Arg;
            }
            window[0] = static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(rng.NextRange(lo, hi)));
            return ScriptTrapCode::None;
        }
    }

    (void)argCount;
    // Physics, movement, cues, and commands need their owning systems present;
    // the behavior bridge supplies component, tag, and random access. A call
    // to an unsupported host function in this context is a deterministic Arg
    // trap rather than silent success.
    return ScriptTrapCode::Arg;
}
