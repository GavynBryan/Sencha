#include <script/WorldScriptHost.h>

#include <core/hash/ContentHash.h>
#include <core/random/DeterministicRandom.h>
#include <ecs/CommandBuffer.h>
#include <ecs/FieldDelta.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <array>
#include <bit>
#include <cstring>
#include <utility>

namespace
{
    // Storage scalar <-> 64-bit register conversions. Floats live
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

    // A per-callback random stream: seeded from the world seed, the subject
    // entity, and the tick, with the import index folded in so distinct random
    // calls in one callback draw from independent streams.
    DeterministicRandom RandomStream(std::uint64_t worldSeed, EntityId subject, std::uint64_t tick,
                                     std::uint32_t importIndex)
    {
        const std::uint64_t streamId =
            PackScriptEntity(subject) ^ (static_cast<std::uint64_t>(importIndex) << 40);
        return DeterministicRandom::FromInputs(worldSeed, streamId, tick);
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
    PendingOutcome = ScriptAbilityOutcome::None;
}

bool WorldScriptHost::WillHaveComponent(std::uint64_t entityBits, ComponentId component)
{
    const auto key = std::make_pair(entityBits, component);
    if (auto it = PendingPresence.find(key); it != PendingPresence.end())
    {
        return it->second;
    }
    // World state is stable across a Step (structural changes are deferred to the
    // flush), so seed once from the live signature and track deltas from there.
    const bool present = W.HasComponent(UnpackScriptEntity(entityBits), component);
    PendingPresence.emplace(key, present);
    return present;
}

void WorldScriptHost::SetPendingPresence(std::uint64_t entityBits, ComponentId component,
                                         bool present)
{
    PendingPresence[std::make_pair(entityBits, component)] = present;
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

ScriptTrapCode WorldScriptHost::ComponentStoreDeferred(const ScriptModule&,
                                                       std::uint64_t entityBits,
                                                       std::uint32_t fieldBind,
                                                       std::uint32_t elementIndex,
                                                       std::span<const std::uint64_t> in)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    const ResolvedFieldBind& bind = Linked->Fields[fieldBind];
    const auto scalar = static_cast<ScriptScalarKind>(bind.Scalar);
    const std::size_t scalarSize = ScriptScalarSize(bind.Scalar);
    const std::size_t byteCount = in.size() * scalarSize;
    // Leaf fields are small (scalars and short vectors); the widest is a Color3
    // packed into 8 bytes. A fixed buffer holds the storage image before it is
    // copied into the command arena.
    std::array<std::byte, 64> storage{};
    if (byteCount == 0 || byteCount > storage.size())
    {
        return ScriptTrapCode::Arg;
    }
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        StoreScalar(storage.data() + i * scalarSize, scalar, in[i]);
    }
    const std::size_t offset =
        bind.Offset + static_cast<std::size_t>(elementIndex) * bind.SlotCount * scalarSize;
    Commands.SetFieldRaw(entity, bind.Component, static_cast<std::uint16_t>(offset), storage.data(),
                         byteCount);
    return ScriptTrapCode::None;
}

ScriptTrapCode WorldScriptHost::ComponentDelta(const ScriptModule&, std::uint64_t entityBits,
                                               std::uint32_t fieldBind, std::uint32_t elementIndex,
                                               std::span<const std::uint64_t> in)
{
    const EntityId entity = UnpackScriptEntity(entityBits);
    if (!W.IsAlive(entity))
    {
        return ScriptTrapCode::Entity;
    }
    if (in.empty())
    {
        return ScriptTrapCode::Arg;
    }
    const ResolvedFieldBind& bind = Linked->Fields[fieldBind];
    const auto scalar = static_cast<ScriptScalarKind>(bind.Scalar);
    // A delta targets one numeric scalar; the addend commutes only for these
    // kinds (integer adds wrap). Non-numeric or multi-slot targets are a trap.
    NumericFieldKind kind{};
    switch (scalar)
    {
    case ScriptScalarKind::Int32: kind = NumericFieldKind::Int32; break;
    case ScriptScalarKind::UInt32: kind = NumericFieldKind::UInt32; break;
    case ScriptScalarKind::Int64: kind = NumericFieldKind::Int64; break;
    case ScriptScalarKind::Float: kind = NumericFieldKind::Float; break;
    case ScriptScalarKind::Double: kind = NumericFieldKind::Double; break;
    default: return ScriptTrapCode::Arg;
    }
    const std::size_t scalarSize = ScriptScalarSize(bind.Scalar);
    std::array<std::byte, 8> addend{};
    StoreScalar(addend.data(), scalar, in[0]);
    const std::size_t offset =
        bind.Offset + static_cast<std::size_t>(elementIndex) * bind.SlotCount * scalarSize;
    Commands.DeltaFieldRaw(entity, bind.Component, static_cast<std::uint16_t>(offset), kind,
                           addend.data(), scalarSize);
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

ScriptHostOp ResolveScriptHostOp(std::string_view name)
{
    struct Entry
    {
        std::string_view Name;
        ScriptHostOp Op;
    };
    static constexpr Entry kOps[] = {
        { "ability.finish", ScriptHostOp::AbilityFinish },
        { "ability.cancel", ScriptHostOp::AbilityCancel },
        { "random.f32", ScriptHostOp::RandomF32 },
        { "random.range", ScriptHostOp::RandomRange },
        { "commands.destroy", ScriptHostOp::CommandsDestroy },
        { "commands.remove", ScriptHostOp::CommandsRemove },
        { "commands.add", ScriptHostOp::CommandsAdd },
        { "cue.fire", ScriptHostOp::CueFire },
        { "cue.fire_at", ScriptHostOp::CueFireAt },
    };
    for (const Entry& entry : kOps)
    {
        if (entry.Name == name)
        {
            return entry.Op;
        }
    }
    return ScriptHostOp::Unknown;
}

ScriptTrapCode WorldScriptHost::HostCall(const ScriptModule& module, std::uint32_t importIndex,
                                         std::span<std::uint64_t> window, std::uint32_t argCount)
{
    (void)argCount;
    switch (static_cast<ScriptHostOp>(Linked->Imports[importIndex].Id))
    {
    // Ability lifecycle: record the outcome for the bridge to act on after the
    // callback returns. The last call wins.
    case ScriptHostOp::AbilityFinish:
        PendingOutcome = ScriptAbilityOutcome::Finish;
        return ScriptTrapCode::None;
    case ScriptHostOp::AbilityCancel:
        PendingOutcome = ScriptAbilityOutcome::Cancel;
        return ScriptTrapCode::None;

    // Deterministic randomness: a per-callback stream seeded from the world
    // seed, the subject entity, and the tick.
    case ScriptHostOp::RandomF32:
    {
        DeterministicRandom rng = RandomStream(Runtime.WorldSeed, Subject, Tick, importIndex);
        window[0] = std::bit_cast<std::uint64_t>(static_cast<double>(rng.NextFloat01()));
        return ScriptTrapCode::None;
    }
    case ScriptHostOp::RandomRange:
    {
        const int32_t lo = static_cast<int32_t>(window[0]);
        const int32_t hi = static_cast<int32_t>(window[1]);
        if (lo > hi)
        {
            return ScriptTrapCode::Arg;
        }
        DeterministicRandom rng = RandomStream(Runtime.WorldSeed, Subject, Tick, importIndex);
        window[0] = static_cast<std::uint64_t>(static_cast<std::uint32_t>(rng.NextRange(lo, hi)));
        return ScriptTrapCode::None;
    }

    // Structural changes: enqueue onto the tick's CommandBuffer (deferred to
    // the flush at the tick boundary), so a callback never restructures the
    // ECS mid-iteration.
    case ScriptHostOp::CommandsDestroy:
    {
        const EntityId target = UnpackScriptEntity(window[0]);
        if (!W.IsAlive(target))
        {
            return ScriptTrapCode::Entity;
        }
        Commands.DestroyEntity(target);
        return ScriptTrapCode::None;
    }
    case ScriptHostOp::CommandsRemove:
    {
        const EntityId target = UnpackScriptEntity(window[0]);
        if (!W.IsAlive(target))
        {
            return ScriptTrapCode::Entity;
        }
        const auto bind = static_cast<std::uint32_t>(window[1]);
        const ComponentId component = Linked->Components[bind];
        // Removing a component the entity does not (and will not) have is a
        // deterministic no-op, not a trap: idempotent remove is friendlier and
        // keeps the flush from asserting on an absent component.
        if (!WillHaveComponent(window[0], component))
        {
            return ScriptTrapCode::None;
        }
        Commands.RemoveComponentRaw(target, component, /*size*/ 0);
        SetPendingPresence(window[0], component, false);
        return ScriptTrapCode::None;
    }
    case ScriptHostOp::CommandsAdd:
    {
        const EntityId target = UnpackScriptEntity(window[0]);
        if (!W.IsAlive(target))
        {
            return ScriptTrapCode::Entity;
        }
        const auto bind = static_cast<std::uint32_t>(window[1]);
        const std::int32_t schemaIndex = Linked->ComponentSchemaIndex[bind];
        if (schemaIndex < 0)
        {
            // Host components are not addable from a script in v1.
            return ScriptTrapCode::Arg;
        }
        const ComponentId component = Linked->Components[bind];
        // Adding a component the entity already has (e.g. one the designer authored)
        // would assert in the CommandBuffer flush and abort the player; reject it as
        // a trap instead. A behavior that needs the component present should declare
        // it with `requires` rather than add it here.
        if (WillHaveComponent(window[0], component))
        {
            return ScriptTrapCode::Structural;
        }
        const ScriptComponentDef& schema =
            Linked->Module->Components[static_cast<std::size_t>(schemaIndex)];
        const ScriptComponentLayout layout = ComputeScriptComponentLayout(schema);
        // Marshal the record image (window[2..]) into component bytes: the
        // image is laid out leaf by leaf in schema order, matching codegen.
        std::vector<std::byte> bytes(layout.Size, std::byte{ 0 });
        std::uint32_t slot = 2;
        for (const ScriptComponentField& field : schema.Fields)
        {
            const auto scalar = static_cast<ScriptScalarKind>(field.Scalar);
            const std::size_t scalarSize = ScriptScalarSize(field.Scalar);
            for (std::uint8_t e = 0; e < std::max<std::uint8_t>(field.ArrayCount, 1); ++e)
            {
                if (slot < window.size())
                {
                    StoreScalar(bytes.data() + field.ByteOffset + e * scalarSize, scalar,
                                window[slot]);
                }
                ++slot;
            }
        }
        Commands.AddComponentRaw(target, component, bytes.data(), layout.Size, layout.Alignment);
        SetPendingPresence(window[0], component, true);
        return ScriptTrapCode::None;
    }

    // Cues: append to the subject's cue buffer, which a presentation system
    // drains. The cue asset path hashes to a stable id (unlike a runtime asset
    // id), so the event is deterministic and build-stable.
    case ScriptHostOp::CueFire:
    case ScriptHostOp::CueFireAt:
    {
        ScriptCueBuffer* buffer = W.TryGet<ScriptCueBuffer>(Subject);
        if (buffer == nullptr)
        {
            return ScriptTrapCode::None; // no sink; drop, matching a full buffer
        }
        const auto bind = static_cast<std::uint32_t>(window[0]);
        ScriptCueEvent event;
        event.CueHash = HashBytes64(module.GetString(module.AssetBinds[bind].Path));
        if (static_cast<ScriptHostOp>(Linked->Imports[importIndex].Id) == ScriptHostOp::CueFireAt)
        {
            for (int i = 0; i < 3; ++i)
            {
                event.Position[i] = static_cast<float>(std::bit_cast<double>(window[1 + i]));
            }
            event.HasPosition = 1;
        }
        buffer->Push(event);
        return ScriptTrapCode::None;
    }

    case ScriptHostOp::Unknown:
        break;
    }

    // Physics and movement need their owning systems present (a physics rig and
    // the movement systems). Unsupported here is a deterministic Arg trap rather
    // than silent success.
    return ScriptTrapCode::Arg;
}
