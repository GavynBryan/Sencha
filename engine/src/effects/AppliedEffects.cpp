#include <effects/AppliedEffects.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/EntityText.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/TypedComponentSerializer.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr std::string_view kEffects = "effects";
    constexpr std::string_view kKey = "key";
    constexpr std::string_view kEffect = "effect";
    constexpr std::string_view kRemainingTicks = "remaining_ticks";
    constexpr std::string_view kTicksToPeriod = "ticks_to_period";
    constexpr std::string_view kScale = "scale";
    constexpr std::string_view kOwner = "owner";
    constexpr std::string_view kOwnerKey = "owner_key";

    struct KeptEffectRow
    {
        std::uint64_t Sequence = 0;
        KeptEffect Effect;
    };

    struct KeptEffectsByTarget
    {
        std::unordered_map<EntityId, std::vector<KeptEffectRow>, EntityIdHash> Targets;
    };

    bool Identified(const World& world, EntityId entity)
    {
        return world.IsAlive(entity) && world.IsRegistered<PersistentIdComponent>()
            && world.HasComponent<PersistentIdComponent>(entity);
    }

    PersistentEntityId IdOf(const World& world, EntityId entity)
    {
        const PersistentIdComponent* identity = Identified(world, entity) ? world.TryGet<PersistentIdComponent>(entity) : nullptr;
        return identity != nullptr ? identity->Id : PersistentEntityId{};
    }

    KeptEffectsByTarget ReadKeptEffects(const World& world)
    {
        KeptEffectsByTarget kept;
        if (!world.IsRegistered<ActiveEffect>())
            return kept;
        Query<Read<ActiveEffect>> query(world);
        query.ForEachChunk([&](auto& view) {
            const auto effects = view.template Read<ActiveEffect>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                const ActiveEffect& active = effects[i];
                if (!active.Kept || !Identified(world, active.Target))
                    continue;
                KeptEffect effect{ .Effect = active.Def,
                                   .RemainingTicks = active.RemainingTicks,
                                   .TicksToPeriod = active.TicksToPeriod,
                                   .Scale = active.Scale,
                                   .Owner = IdOf(world, active.Owner.Entity),
                                   .OwnerKey = active.Owner.Key };
                if (!effect.Owner.IsValid())
                    effect.OwnerKey = 0;
                kept.Targets[active.Target].push_back({ active.Sequence, effect });
            }
        });
        for (auto& [target, rows] : kept.Targets)
            std::ranges::sort(rows, {}, &KeptEffectRow::Sequence);
        return kept;
    }

    void KeepTheLongest(std::vector<KeptEffectRow>& rows)
    {
        std::ranges::stable_sort(rows, std::ranges::greater{},
                                 [](const KeptEffectRow& row) { return row.Effect.RemainingTicks; });
        rows.resize(kAppliedEffectCapacity);
        std::ranges::sort(rows, {}, &KeptEffectRow::Sequence);
    }

    std::vector<KeptEffect> EntriesOf(const World& world, EntityId entity, const AppliedEffects& row)
    {
        if (row.Count > 0)
            return { row.Entries, row.Entries + std::min<std::uint8_t>(row.Count, kAppliedEffectCapacity) };
        std::vector<KeptEffect> entries;
        if (const KeptEffectsByTarget* kept = world.TryGetResource<KeptEffectsByTarget>())
            if (const auto found = kept->Targets.find(entity); found != kept->Targets.end())
                for (const KeptEffectRow& effect : found->second)
                    entries.push_back(effect.Effect);
        return entries;
    }

class AppliedEffectsSerializer final : public TypedComponentSerializer<AppliedEffects>
{
public:
    std::string_view JsonKey() const override { return "AppliedEffects"; }
    std::uint32_t BinaryChunkId() const override { return MakeFourCC('A', 'E', 'F', 'X'); }
    std::span<const RuntimeField> RuntimeFields() const override { return {}; }
    bool Authorable() const override { return false; }

    const DataFieldSchema* CompositionSchema() const override
    {
        static const DataFieldSchema schema = MakeKeyedListRecordSchema(std::string(kEffects), std::string(kKey));
        return &schema;
    }

    bool Save(IWriteArchive& archive, EntityId entity, const World& world, SceneSerializationContext&) const override
    {
        const AppliedEffects* row = world.IsRegistered<AppliedEffects>() ? world.TryGet<AppliedEffects>(entity) : nullptr;
        if (row == nullptr)
            return true;
        if (!archive.IsText())
        {
            archive.MarkInvalidField(kEffects);
            return false;
        }
        const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
        const std::vector<KeptEffect> entries = EntriesOf(world, entity, *row);
        std::unordered_map<EffectId, std::uint32_t> ordinals;
        archive.BeginObject(std::string_view{});
        archive.BeginArray(kEffects, entries.size());
        for (const KeptEffect& entry : entries)
        {
            const std::string_view name = effects != nullptr ? effects->GetName(entry.Effect) : std::string_view{};
            archive.BeginObject(std::string_view{});
            archive.Field(kKey, std::string_view(std::format("{}#{}", name, ordinals[entry.Effect]++)));
            archive.Field(kEffect, name);
            if (entry.RemainingTicks != kEffectNeverEnds)
                archive.Field(kRemainingTicks, entry.RemainingTicks);
            if (entry.TicksToPeriod != 0)
                archive.Field(kTicksToPeriod, entry.TicksToPeriod);
            if (entry.Scale != 1.0f)
                archive.Field(kScale, entry.Scale);
            if (entry.Owner.IsValid())
            {
                archive.Field(kOwner, std::string_view(PersistentEntityIdToString(entry.Owner)));
                archive.Field(kOwnerKey, std::string_view(std::to_string(entry.OwnerKey)));
            }
            archive.End();
        }
        archive.End();
        archive.End();
        return archive.Ok();
    }

    bool SaveDefault(IWriteArchive& archive) const override
    {
        if (!archive.IsText())
            return false;
        archive.BeginObject(std::string_view{});
        archive.BeginArray(kEffects, 0);
        archive.End();
        archive.End();
        return archive.Ok();
    }

    bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                       SceneSerializationContext& context) override
    {
        if (!archive.IsText())
        {
            archive.MarkInvalidField(kEffects);
            return false;
        }
        const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
        AppliedEffects row;
        archive.BeginObject(std::string_view{});
        std::size_t count = 0;
        if (archive.HasField(kEffects))
            archive.BeginArray(kEffects, count);
        for (std::size_t i = 0; i < count && archive.Ok(); ++i)
        {
            archive.BeginObject(std::string_view{});
            std::string name;
            archive.Field(kEffect, name);
            KeptEffect entry;
            entry.Effect = effects != nullptr ? effects->Find(name) : EffectId{};
            if (archive.HasField(kRemainingTicks))
                archive.Field(kRemainingTicks, entry.RemainingTicks);
            if (archive.HasField(kTicksToPeriod))
                archive.Field(kTicksToPeriod, entry.TicksToPeriod);
            if (archive.HasField(kScale))
                archive.Field(kScale, entry.Scale);
            if (archive.HasField(kOwner))
            {
                std::string owner;
                std::string key;
                archive.Field(kOwner, owner);
                if (archive.HasField(kOwnerKey))
                    archive.Field(kOwnerKey, key);
                entry.Owner = PersistentEntityIdFromString(owner).value_or(PersistentEntityId{});
                entry.OwnerKey = key.empty() ? 0 : std::stoull(key);
            }
            archive.End();
            if (!entry.Effect.IsValid())
            {
                context.Logging->GetLogger<AppliedEffects>().Warn(
                    "AppliedEffects: no effect named '{}' is defined; the entity is not put back under it", name);
                continue;
            }
            if (row.Count < kAppliedEffectCapacity)
                row.Entries[row.Count++] = entry;
        }
        if (count > 0)
            archive.End();
        archive.End();
        if (!archive.Ok())
            return false;
        return Place(world, entity, row);
    }
};
} // namespace

std::size_t SettleAppliedEffects(World& world)
{
    const ZoneStateSerialization* serialization = world.TryGetResource<ZoneStateSerialization>();
    if (serialization == nullptr || !world.IsRegistered<AppliedEffects>())
        return 0;
    LoggingProvider* logging = serialization->Context != nullptr ? serialization->Context->Logging : nullptr;
    KeptEffectsByTarget kept = ReadKeptEffects(world);
    std::vector<EntityId> targets;
    for (const auto& [target, rows] : kept.Targets)
        targets.push_back(target);
    std::ranges::sort(targets, {}, [](EntityId entity) { return entity.Index; });
    std::size_t leftOut = 0;
    for (const EntityId target : targets)
    {
        std::vector<KeptEffectRow>& rows = kept.Targets[target];
        if (rows.size() > kAppliedEffectCapacity)
        {
            leftOut += rows.size() - kAppliedEffectCapacity;
            if (logging != nullptr)
            {
                std::string name;
                AppendEntityText(name, target);
                logging->GetLogger<AppliedEffects>().Error(
                    "AppliedEffects: {} is under {} kept effects and a save keeps {}; those with the least time left "
                    "are left out",
                    name, rows.size(), kAppliedEffectCapacity);
            }
            KeepTheLongest(rows);
        }
        if (!world.HasComponent<AppliedEffects>(target))
            world.AddComponent<AppliedEffects>(target, AppliedEffects{});
    }
    world.SetResource(std::move(kept));
    return leftOut;
}

std::vector<EntityId> OverfullAppliedEffects(const World& world)
{
    std::vector<EntityId> overfull;
    if (!KeepsLevelState(world))
        return overfull;
    for (const auto& [target, rows] : ReadKeptEffects(world).Targets)
        if (rows.size() > kAppliedEffectCapacity)
            overfull.push_back(target);
    std::ranges::sort(overfull, {}, [](EntityId entity) { return entity.Index; });
    return overfull;
}

void RestoreAppliedEffects(World& world, const FixedSimTime& now, const StoragePartitionSet& partitions)
{
    if (!world.IsRegistered<AppliedEffects>())
        return;
    std::vector<std::pair<EntityId, AppliedEffects>> loaded;
    Query<Read<AppliedEffects>> query(world);
    query.ForEachChunk([&](auto& view) {
        const auto rows = view.template Read<AppliedEffects>();
        const EntityIndex* indices = view.Entities();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            if (rows[i].Count == 0)
                continue;
            const EntityId entity = world.ResolveEntityIndex(indices[i]);
            if (partitions.Contains(world.GetEntityPartition(entity)))
                loaded.emplace_back(entity, rows[i]);
        }
    });
    for (const auto& [entity, row] : loaded)
    {
        world.TryGet<AppliedEffects>(entity)->Count = 0;
        for (std::uint8_t i = 0; i < row.Count; ++i)
            (void)RestoreEffect(world, entity, row.Entries[i], now);
    }
}

std::unique_ptr<IComponentSerializer> MakeAppliedEffectsSerializer()
{
    return std::make_unique<AppliedEffectsSerializer>();
}
