#include <zone/ZonePackageImporter.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <core/serialization/Serialize.h>
#include <math/MathSchemas.h>
#include <physics/SensorOverlaps.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/HeldRecordForms.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <zone/GainedSpawnRequests.h>
#include <zone/KeptWithZone.h>
#include <zone/ZoneCaptureImage.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
void SetError(ZoneImportError* error, std::string message)
{
    if (error != nullptr)
        error->Message = std::move(message);
}

// Every path that builds an entity from data owes the derived world transform,
// and there is more than one of them, so the obligation itself lives in the
// transform module. The registration check stays here because whether the
// destination world has the pair at all is a property of the world, resolved
// once per import rather than per entity.
void SeedDerivedTransform(World& world, EntityId entity, bool worldHasTransforms)
{
    if (!worldHasTransforms)
        return;
    SeedDerivedWorldTransform(world, entity);
}

// PackageEntity::PersistentId is import metadata lifted from the entity's
// persistent_id component at package build, and the two decide different things:
// the metadata drives destroyed-entity suppression before the row exists, the
// component drives PersistentEntityIndex registration after it does. A package
// whose two copies disagree would suppress under one identity and resolve under
// another, so agreement is checked rather than assumed. A world that never
// registered the component (minimal fixtures) reads as no identity, which only
// agrees with metadata that is likewise unset.
bool PersistentIdentityAgrees(const World& world,
                              EntityId entity,
                              PersistentEntityId metadata)
{
    PersistentEntityId component;
    if (world.IsRegistered<PersistentIdComponent>())
        if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
            component = id->Id;
    return component == metadata;
}

// Every column the entity will carry, so its row is built once. Declared
// component types, everything those owe, the derived WorldTransform, and Parent
// only when this entity actually has a parent — an unwritten Parent column would
// silently reparent the entity to a null id.
//
// The owed set is in the signature rather than added afterwards for the same
// reason the declared components are: the typed add would migrate the row once
// per owed component, each migration copying every column added before it. A
// pawn owes eleven. `owed` comes back holding the ones the package did not
// declare for itself, which are the columns this import still has to write.
bool BuildEntitySignature(
    const World& world,
    const WorldComponentSchema& schema,
    const PackageEntity& packageEntity,
    bool parented,
    ArchetypeSignature& signature,
    std::vector<ComponentTypeId>& owed,
    std::string& failure)
{
    signature.reset();
    owed.clear();
    bool hasLocalTransform = false;

    for (const PackageComponent& component : packageEntity.Components)
    {
        const WorldComponentSchema::Entry* entry = schema.Find(component.Type);
        if (entry == nullptr)
        {
            failure =
                "Package contains a component absent from the runtime schema.";
            return false;
        }

        const ComponentId id = world.GetComponentIdByType(component.Type);
        if (id == InvalidComponentId)
        {
            failure =
                "Package component is not registered in the destination world.";
            return false;
        }
        signature.set(id);
        hasLocalTransform =
            hasLocalTransform
            || component.Type == ResolveComponentTypeId<LocalTransform>();

        // A world with a partial vocabulary skips what it never registered,
        // exactly as the typed add does.
        for (const ComponentTypeId type : entry->Owed)
        {
            const ComponentId owedId = world.GetComponentIdByType(type);
            if (owedId == InvalidComponentId)
                continue;
            signature.set(owedId);
            owed.push_back(type);
        }
    }

    // What the package declares, it writes itself. Anything left is this
    // import's to default.
    std::erase_if(owed, [&](ComponentTypeId type) {
        return std::any_of(packageEntity.Components.begin(),
                           packageEntity.Components.end(),
                           [&](const PackageComponent& component)
                           { return component.Type == type; });
    });
    std::sort(owed.begin(), owed.end(),
              [](ComponentTypeId a, ComponentTypeId b) { return a.Value < b.Value; });
    owed.erase(std::unique(owed.begin(), owed.end()), owed.end());

    if (hasLocalTransform && world.IsRegistered<WorldTransform>())
        signature.set(world.GetComponentId<WorldTransform>());
    if (parented && world.IsRegistered<Parent>())
        signature.set(world.GetComponentId<Parent>());
    return true;
}

// Writes the columns the entity owes but the package did not carry, at their
// member initializers and with their OnAdd firing — the same thing the typed
// add would have produced, without the row migration each one would have cost.
bool WriteOwedComponents(World& world,
                         EntityId entity,
                         const WorldComponentSchema& schema,
                         std::span<const ComponentTypeId> owed,
                         std::vector<std::byte>& scratch,
                         std::string& failure)
{
    for (const ComponentTypeId type : owed)
    {
        const WorldComponentSchema::Entry* entry = schema.Find(type);
        if (entry == nullptr)
        {
            failure = "A component's derived set names a type the schema does "
                      "not carry.";
            return false;
        }

        scratch.assign(entry->Size, std::byte{});
        if (!schema.WriteDefaultBytes(type, scratch)
            || !schema.InitializeComponent(world, entity, type, scratch))
        {
            failure = "Could not provide the derived component '"
                + std::string(entry->Name) + "'.";
            return false;
        }
    }
    return true;
}

struct ZonePackageImport
{
};

bool ImportComponent(
    World& world,
    EntityId entity,
    const WorldComponentSchema& schema,
    const PackageComponent& component,
    std::span<const PackageComponent> unpatched,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    std::string& failure)
{
    const WorldComponentSchema::Entry* entry = schema.Find(component.Type);
    if (entry == nullptr)
    {
        failure =
            "Package contains a component absent from the runtime schema.";
        return false;
    }

    // Every failure below names the component: an import that says only "a
    // component failed" leaves a whole prefab to bisect by hand.
    const std::string named = std::string(" (") + std::string(entry->Name) + ")";

    if (component.SerializedJson.has_value())
    {
        if (serializers == nullptr || sceneContext == nullptr)
        {
            failure =
                "Package contains serialized data without an import context" + named + ".";
            return false;
        }

        IComponentSerializer* serializer =
            serializers->FindByType(component.Type);
        if (serializer == nullptr)
        {
            failure =
                "Package serialized component has no registered decoder" + named + ".";
            return false;
        }

        JsonReadArchive archive(*component.SerializedJson);
        const bool loaded = serializer->LoadIntoWorld(archive, entity, world, *sceneContext);
        if (loaded && archive.Ok())
            return true;
        const auto content = std::ranges::find(unpatched, component.Type, &PackageComponent::Type);
        if (loaded || content == unpatched.end())
        {
            failure = "Package serialized component decode failed" + named + ".";
            return false;
        }
        sceneContext->Logging->GetLogger<ZonePackageImport>().Warn(
            "the saved {} does not decode; the entity keeps content's", entry->Name);
        return ImportComponent(world, entity, schema, *content, {}, serializers, sceneContext, failure);
    }

    if (entry->Size != component.RuntimeBytes.size())
    {
        failure =
            "Package component byte size does not match the runtime schema"
            + named + ".";
        return false;
    }
    if (!schema.InitializeComponent(
            world,
            entity,
            component.Type,
            component.RuntimeBytes))
    {
        failure = "Package component import failed.";
        return false;
    }
    return true;
}

// An entity's own lifetime, else the nearest ancestor's, else the one authored
// elsewhere in the placed scene it belongs to.
const ZoneStateLifetime* LifetimeOf(const World& world, EntityId entity,
                                    const std::unordered_map<std::uint64_t, ZoneStateLifetime>& byInstance)
{
    for (EntityId at = entity; at.IsValid() && world.IsAlive(at);)
    {
        if (const ZoneStateLifetime* lifetime = world.TryGet<ZoneStateLifetime>(at))
            return lifetime;
        const Parent* parent = world.IsRegistered<Parent>() ? world.TryGet<Parent>(at) : nullptr;
        at = parent != nullptr ? parent->Entity : EntityId{};
    }
    const SceneInstance* instance = world.IsRegistered<SceneInstance>() ? world.TryGet<SceneInstance>(entity) : nullptr;
    const auto found = instance != nullptr ? byInstance.find(instance->Id.Value) : byInstance.end();
    return found != byInstance.end() ? &found->second : nullptr;
}

std::vector<bool> AuthorityOnlyMembers(const World& world, const WorldComponentSchema& schema,
                                       const EntityBuildPackage& package)
{
    std::vector<bool> skipped(package.EntityCount(), false);
    if (IsSimulationAuthority(world))
        return skipped;
    std::size_t index = 0;
    for (const PackageEntity& entity : package.Entities())
        skipped[index++] = IsAuthorityMaterialized(schema, entity);
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const PackageParent& relation : package.Parents())
            if (skipped[relation.Parent.Value] && !skipped[relation.Child.Value])
                grew = skipped[relation.Child.Value] = true;
    }
    return skipped;
}

struct LevelEntityPatches
{
    ZoneStateStore* Store = nullptr;
    const ComponentSerializerRegistry* Serializers = nullptr;
    SceneSerializationContext* Context = nullptr;
    ZoneStateScope Scope;
};

LevelEntityPatches KeptLevelState(World& world, ZoneStateStore* store, ZoneStateScope scope,
                                  const ComponentSerializerRegistry* serializers, SceneSerializationContext* context)
{
    const ZoneStateSerialization* kept = world.TryGetResource<ZoneStateSerialization>();
    if (store == nullptr || serializers == nullptr || context == nullptr || kept == nullptr
        || kept->Serializers == nullptr || !IsSimulationAuthority(world))
        return {};
    return LevelEntityPatches{ store, serializers, context, scope };
}

std::optional<PackageEntity> Patched(const PackageEntity& content, const LevelEntityPatches& patches)
{
    const JsonValue* patch = patches.Store->PatchOf(patches.Scope, content.PersistentId);
    if (patch == nullptr)
        return std::nullopt;
    PackageEntity patched = content;
    const EntityPatchOutcome outcome = ApplyEntityPatch(patched, *patch, *patches.Serializers);
    Logger& log = patches.Context->Logging->GetLogger<ZonePackageImport>();
    const std::string id = PersistentEntityIdToString(content.PersistentId);
    for (const std::string& line : outcome.Retuned)
        log.Warn("zone state: {}: what was kept of {} is set aside; the scene decides", id, line);
    for (const std::string& line : outcome.Misfit)
        log.Error("zone state: {}: what was kept no longer fits its scene and follows content: {}", id, line);
    for (const std::string& member : outcome.MisfitMembers)
        patches.Store->ForgetPatchMember(patches.Scope, content.PersistentId, member);
    return patched;
}

void ReportCarriedMembers(const ZoneStateStore& store, ZoneStateScope scope, SceneSerializationContext* context)
{
    if (context == nullptr)
        return;
    std::vector<std::string_view> forms;
    for (const GainedEntityState& member : store.GainedOf(scope))
        if (!member.Id.IsValid() && std::ranges::find(forms, member.Member.Form) == forms.end())
            forms.push_back(member.Member.Form);
    for (const std::string_view form : forms)
        context->Logging->GetLogger<ZonePackageImport>().Warn(
            "zone state: what the zone gained in the form '{}' is kept and not made; this build does not make it",
            form);
}

std::optional<GainedSpawnRequest> SourceRequest(const ZoneListEntity& saved, ZoneStateScope scope,
                                                StoragePartitionId partition)
{
    const JsonValue* source = saved.Value.Find("source");
    const JsonValue* root = saved.Value.Find("root");
    const JsonValue* patch = saved.Value.Find("patch");
    GainedSpawnRequest request;
    request.Scope = scope;
    request.Partition = partition;
    request.Id = saved.Id;
    const std::optional<AssetId> asset =
        source != nullptr && source->IsString() ? AssetIdFromString(source->AsString()) : std::nullopt;
    if (!asset.has_value() || root == nullptr || !FromJson(*root, request.Root) || patch == nullptr || !patch->IsObject())
        return std::nullopt;
    request.Source = *asset;
    request.Patch = *patch;
    return request;
}

void MakeGainedMembers(World& world, StoragePartitionId partition, const LevelEntityPatches& patches,
                       const EntityBuildPackage& package, bool worldHasTransforms, std::vector<EntityId>& made)
{
    const std::span<const GainedEntityState> recorded = patches.Store->GainedOf(patches.Scope);
    if (recorded.empty() || !world.IsRegistered<KeptWithZone>() || !world.IsRegistered<PersistentIdComponent>())
        return;
    const std::vector<GainedEntityState> gained(recorded.begin(), recorded.end());
    Logger& log = patches.Context->Logging->GetLogger<ZonePackageImport>();
    ZoneCaptureImager imager(world, *patches.Serializers);
    const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
    for (const GainedEntityState& member : gained)
    {
        if (!member.Id.IsValid())
            continue;
        const std::string named = PersistentEntityIdToString(member.Id);
        const std::optional<ZoneListEntity> saved = DecodeGainedMember(member.Member.Payload);
        const bool fromSource = member.Member.Form == kSourceGainedForm;
        const std::optional<GainedSpawnRequest> request =
            saved.has_value() && fromSource ? SourceRequest(*saved, patches.Scope, partition) : std::nullopt;
        const bool authoredHere = saved.has_value() && IsAuthoredPersistentEntityId(saved->Id)
            && std::ranges::any_of(package.Entities(),
                                   [&](const PackageEntity& entity) { return entity.PersistentId == saved->Id; });
        if (patches.Store->IsRecordedDeparted(patches.Scope, member.Id))
        {
            patches.Store->ForgetGained(patches.Scope, member.Id);
            continue;
        }
        if (!saved.has_value() || authoredHere || (fromSource && !request.has_value()))
        {
            log.Error("zone state: what the zone gained, {}, is not an entity its zone did not author, written {};"
                      " it is dropped",
                      named, member.Member.Form);
            patches.Store->ForgetGained(patches.Scope, member.Id);
            continue;
        }
        if (identities != nullptr && world.IsAlive(identities->TryResolve(saved->Id)))
        {
            log.Warn("zone state: what the zone gained, {}, is not made; a live entity already has its id", named);
            continue;
        }
        if (request.has_value())
        {
            GainedSpawnRequests& spawning = world.HasResource<GainedSpawnRequests>()
                ? world.GetResource<GainedSpawnRequests>()
                : world.AddResource<GainedSpawnRequests>();
            spawning.Add(*request);
            continue;
        }
        const EntityId entity = world.CreateEntity(partition);
        world.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ saved->Id });
        world.AddComponent<KeptWithZone>(entity);
        if (std::string failed; !ReadEntityRecord(world, entity, saved->Value, *patches.Serializers,
                                                  *patches.Context, &failed))
        {
            log.Error("zone state: what the zone gained, {}, does not read its '{}'; it is dropped", named, failed);
            world.DestroyEntity(entity);
            patches.Store->ForgetGained(patches.Scope, member.Id);
            continue;
        }
        SeedDerivedTransform(world, entity, worldHasTransforms);
        patches.Store->RecordGainedImage(patches.Scope, member.Id, imager.Gained(entity));
        made.push_back(entity);
    }
}

void MakeHeldRecords(World& world, StoragePartitionId partition, ZoneStateStore& store, ZoneStateScope scope,
                     const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
{
    const std::vector<ZoneListEntity> held = store.TakeHeld(scope);
    if (held.empty())
        return;
    std::vector<EntityId> made;
    for (const ZoneListEntity& record : held)
        if (const EntityId entity = MakeHeldRecord(world, partition, record, serializers, context); entity.IsValid())
            made.push_back(entity);
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr)
        return;
    const GainedSpawnRequests* spawning = world.TryGetResource<GainedSpawnRequests>();
    std::vector<PersistentEntityId> absent;
    for (const EntityId entity : made)
        if (const PersistentEntityId holder = world.IsAlive(entity) ? index->AwaitedHolderOf(entity) : PersistentEntityId{};
            holder.IsValid() && std::ranges::find(absent, holder) == absent.end()
            && (spawning == nullptr || !spawning->AwaitsHolder(holder)))
            absent.push_back(holder);
    for (const PersistentEntityId holder : absent)
        context.Logging->GetLogger<ZonePackageImport>().Error(
            "zone state: {} held records waiting for {}, which did not arrive, are dropped",
            DropRecordsAwaiting(world, holder), PersistentEntityIdToString(holder));
}

struct BaselineRules
{
    const World& Entities;
    const WorldComponentSchema& Schema;
    const LevelEntityPatches& Patches;
    std::vector<ComponentTypeId> BytesReported;
    std::vector<std::pair<ComponentTypeId, std::optional<JsonValue>>> OwedDefaults;
};

bool HoldsPlacement(const PackageEntity& content, const ComponentSerializerRegistry& serializers)
{
    return std::ranges::any_of(content.Components, [&](const PackageComponent& component) {
        const IComponentSerializer* serializer = serializers.FindByType(component.Type);
        return serializer != nullptr && component.SerializedJson.has_value()
            && serializer->HoldsPlacement(*component.SerializedJson);
    });
}

const std::optional<JsonValue>& OwedDefault(BaselineRules& rules, ComponentTypeId type)
{
    const auto cached = std::ranges::find(rules.OwedDefaults, type, &std::pair<ComponentTypeId, std::optional<JsonValue>>::first);
    if (cached != rules.OwedDefaults.end())
        return cached->second;
    const IComponentSerializer* serializer = rules.Patches.Serializers->FindByType(type);
    return rules.OwedDefaults
        .emplace_back(type, serializer != nullptr ? SavedDefaultRecord(*serializer) : std::nullopt)
        .second;
}

void AddOwedDefaults(BaselineRules& rules, const PackageEntity& content, std::vector<AuthoredComponent>& baseline)
{
    for (const PackageComponent& component : content.Components)
    {
        const WorldComponentSchema::Entry* entry = rules.Schema.Find(component.Type);
        if (entry == nullptr)
            continue;
        for (const ComponentTypeId owed : entry->Owed)
        {
            if (content.Find(owed) != nullptr || rules.Entities.GetComponentIdByType(owed) == InvalidComponentId
                || std::ranges::find(baseline, owed, &AuthoredComponent::Type) != baseline.end())
                continue;
            if (const std::optional<JsonValue>& record = OwedDefault(rules, owed); record.has_value())
                baseline.push_back({ .Type = owed, .Record = *record, .InContent = true, .Diffed = true });
        }
    }
}

std::vector<AuthoredComponent> BaselineOf(const PackageEntity& content, ZoneStatePlacement placement,
                                          BaselineRules& rules)
{
    const LevelEntityPatches& patches = rules.Patches;
    const ComponentTypeId transform = ResolveComponentTypeId<LocalTransform>();
    const bool placed = placement == ZoneStatePlacement::Kept
        || (placement == ZoneStatePlacement::ByComponents && HoldsPlacement(content, *patches.Serializers));
    std::vector<AuthoredComponent> baseline = AuthoredComponentsOf(
        content, *patches.Serializers,
        { .Listed = placed ? std::span<const ComponentTypeId>(&transform, 1) : std::span<const ComponentTypeId>(),
          .Declared = { .Saved = true } });
    AddOwedDefaults(rules, content, baseline);
    std::vector<ComponentTypeId>& bytesReported = rules.BytesReported;
    for (AuthoredComponent& component : baseline)
    {
        if (!component.InContent || !component.Record.IsNull())
            continue;
        component.Diffed = false;
        if (std::ranges::find(bytesReported, component.Type) != bytesReported.end())
            continue;
        bytesReported.push_back(component.Type);
        const IComponentSerializer* serializer = patches.Serializers->FindByType(component.Type);
        patches.Context->Logging->GetLogger<ZonePackageImport>().Warn(
            "zone state: '{}' is cooked as bytes in this zone, so changes to it are not kept",
            serializer != nullptr ? serializer->JsonKey() : std::string_view("?"));
    }
    return baseline;
}

std::vector<AuthoredEntityState> AuthoredStates(const World& world, const WorldComponentSchema& schema,
                                                const EntityBuildPackage& package, std::span<const EntityId> entities,
                                                const std::vector<bool>& authorityOnly, const ZoneStateStore& store,
                                                ZoneStateScope scope, const LevelEntityPatches& patches)
{
    BaselineRules rules{ .Entities = world, .Schema = schema, .Patches = patches, .BytesReported = {}, .OwedDefaults = {} };
    std::unordered_map<std::uint64_t, ZoneStateLifetime> byInstance;
    const bool lifetimes = world.IsRegistered<ZoneStateLifetime>();
    if (lifetimes && world.IsRegistered<SceneInstance>())
        for (const EntityId entity : entities)
            if (const ZoneStateLifetime* lifetime = entity.IsValid() ? world.TryGet<ZoneStateLifetime>(entity) : nullptr)
                if (const SceneInstance* instance = world.TryGet<SceneInstance>(entity))
                    byInstance.emplace(instance->Id.Value, *lifetime);

    std::vector<AuthoredEntityState> authored;
    authored.reserve(package.EntityCount());
    std::optional<ZoneCaptureImager> imager;
    std::size_t index = 0;
    for (const PackageEntity& packageEntity : package.Entities())
    {
        const bool materializedElsewhere = authorityOnly[index];
        const EntityId entity = entities[index++];
        if (materializedElsewhere || !packageEntity.PersistentId.IsValid())
            continue;
        const ZoneStateLifetime* lifetime =
            entity.IsValid() && lifetimes ? LifetimeOf(world, entity, byInstance) : nullptr;
        const ZoneStateForgetting forgotten = !entity.IsValid() ? store.ForgettingOf(scope, packageEntity.PersistentId)
                                              : lifetime != nullptr ? lifetime->Forgotten
                                                                    : ZoneStateForgetting::Never;
        const bool diffed = patches.Store != nullptr && entity.IsValid() && forgotten != ZoneStateForgetting::AtDetach;
        AuthoredEntityState& state = authored.emplace_back(AuthoredEntityState{
            packageEntity.PersistentId, forgotten,
            diffed ? BaselineOf(packageEntity, lifetime != nullptr ? lifetime->Placement : ZoneStatePlacement::ByComponents,
                                rules)
                   : std::vector<AuthoredComponent>{},
            {} });
        if (diffed && patches.Store->PatchOf(scope, packageEntity.PersistentId) == nullptr)
        {
            if (!imager.has_value())
                imager.emplace(world, *patches.Serializers);
            state.Image = imager->Authored(entity, state.Baseline);
        }
    }
    return authored;
}

void SeedOccupancy(World& world, const ZoneStateStore& store, ZoneStateScope scope)
{
    const std::vector<SensorSeed> pairs = store.OccupiedOf(scope);
    if (pairs.empty())
        return;
    SensorOverlaps& overlaps =
        world.HasResource<SensorOverlaps>() ? world.GetResource<SensorOverlaps>() : world.AddResource<SensorOverlaps>();
    overlaps.ForgetAwaitedIf([&](const SensorSeed& seed) {
        return std::ranges::any_of(pairs, [&](const SensorSeed& pair) { return pair.Sensor == seed.Sensor; });
    });
    overlaps.Await(pairs);
}

bool ImportPackageIntoPartitionImpl(
    World& world,
    const WorldComponentSchema& schema,
    const EntityBuildPackage& package,
    StoragePartitionId partition,
    ZoneStateScope stateScope,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    ZoneImportError* error,
    std::vector<EntityId>* created)
{
    std::vector<EntityId> entities;
    entities.reserve(package.EntityCount());

    const auto fail = [&](std::string message)
    {
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            if (it->IsValid() && world.IsAlive(*it))
                world.DestroyEntity(*it);
        SetError(error, std::move(message));
        return false;
    };

    // The zone state overlay: entities recorded destroyed in a prior residency
    // are not re-created. Worlds without the store (editor documents, plain
    // test worlds) import the cooked scene verbatim, and so does an import that
    // names no scope -- content built for this import alone has no prior
    // residency to have destroyed anything.
    ZoneStateStore* zoneState =
        stateScope.IsValid() ? world.TryGetResource<ZoneStateStore>() : nullptr;
    const LevelEntityPatches patches = KeptLevelState(world, zoneState, stateScope, serializers, sceneContext);
    if (zoneState != nullptr)
        ReportCarriedMembers(*zoneState, stateScope, sceneContext);

    // Which entities are parented has to be known before any row is built, so
    // Parent joins the initial signature instead of costing a later archetype
    // transition per child.
    std::vector<bool> parented(package.EntityCount(), false);
    for (const PackageParent& relation : package.Parents())
    {
        if (!package.ContainsEntity(relation.Child)
            || !package.ContainsEntity(relation.Parent))
        {
            return fail("Package hierarchy references an unknown entity.");
        }
        parented[relation.Child.Value] = true;
    }
    for (const PackageEntity& packageEntity : package.Entities())
        if (packageEntity.PersistentId.IsValid() && !IsAuthoredPersistentEntityId(packageEntity.PersistentId))
            return fail("Package entity holds a persistent id from the runtime namespace.");

    // Resolved once: per entity these are two hash lookups that cannot change
    // mid-import, because component registration is sealed before any entity
    // exists.
    const bool worldHasTransforms =
        world.IsRegistered<LocalTransform>() && world.IsRegistered<WorldTransform>();

    const std::vector<bool> authorityOnly = AuthorityOnlyMembers(world, schema, package);

    // Every member's handle exists before any component is read, so references between members resolve.
    std::vector<PackageMember> members;
    for (const PackageEntity& packageEntity : package.Entities())
    {
        const bool skipped = authorityOnly[entities.size()]
            || (zoneState != nullptr && packageEntity.PersistentId.IsValid()
                && (zoneState->IsRecordedDestroyed(stateScope, packageEntity.PersistentId)
                    || zoneState->IsRecordedDeparted(stateScope, packageEntity.PersistentId)));
        entities.push_back(skipped ? EntityId{} : world.CreateEntity(partition));
        if (packageEntity.MemberId.IsValid())
            members.push_back({ packageEntity.MemberId, entities.back(), packageEntity.PersistentId });
    }
    std::ranges::sort(members, {}, &PackageMember::Authored);
    struct MembersInScope
    {
        SceneSerializationContext* Context;
        ~MembersInScope()
        {
            if (Context != nullptr)
                Context->PackageMembers = {};
        }
    } membersInScope{ sceneContext };
    if (sceneContext != nullptr)
        sceneContext->PackageMembers = members;

    ArchetypeSignature signature;
    // Reused across entities: an import of a thousand pawns should not allocate
    // a thousand owed lists.
    std::vector<ComponentTypeId> owed;
    std::vector<std::byte> owedDefaults;
    std::size_t packageIndex = 0;
    for (const PackageEntity& content : package.Entities())
    {
        const EntityId entity = entities[packageIndex];
        if (!entity.IsValid())
        {
            ++packageIndex;
            continue;
        }
        const std::optional<PackageEntity> patched = patches.Store != nullptr && content.PersistentId.IsValid()
            ? Patched(content, patches)
            : std::nullopt;
        const PackageEntity& packageEntity = patched.has_value() ? *patched : content;

        std::string failure;
        if (!BuildEntitySignature(
                world,
                schema,
                packageEntity,
                parented[packageIndex],
                signature,
                owed,
                failure))
        {
            return fail(std::move(failure));
        }
        ++packageIndex;

        // One row at its final archetype, then every column written in place.
        // Growing the entity component by component would migrate its row once
        // per component, each migration copying the columns added before it.
        world.AssignEmptyEntitySignature(entity, signature);

        for (const PackageComponent& component : packageEntity.Components)
        {
            if (!ImportComponent(
                    world,
                    entity,
                    schema,
                    component,
                    packageEntity.Unpatched,
                    serializers,
                    sceneContext,
                    failure))
            {
                return fail(std::move(failure));
            }
        }

        if (!WriteOwedComponents(world, entity, schema, owed, owedDefaults, failure))
            return fail(std::move(failure));

        SeedDerivedTransform(world, entity, worldHasTransforms);

        if (!PersistentIdentityAgrees(world, entity, packageEntity.PersistentId))
        {
            return fail("Package entity identity metadata disagrees with its "
                        "persistent_id component.");
        }
    }

    for (const PackageParent& relation : package.Parents())
    {
        const EntityId child = entities[relation.Child.Value];
        const EntityId parent = entities[relation.Parent.Value];
        // A suppressed end leaves the relation unwired: the child imports
        // unparented, matching how destruction leaves a live orphan rather
        // than cascading.
        if (!child.IsValid() || !parent.IsValid())
            continue;
        // Pre-created by BuildEntitySignature whenever Parent is registered;
        // the add is the path for a world that registered it later.
        if (!world.IsRegistered<Parent>()
            || !world.InitializeComponent<Parent>(child, Parent{ parent }))
        {
            world.AddComponent<Parent>(child, Parent{ parent });
        }
    }

    // Successful import records what the cooked scene authored, so the detach
    // capture can diff live state against it. Scopeless imports record nothing:
    // there is no residency for a later detach to diff.
    if (zoneState != nullptr)
    {
        std::vector<AuthoredEntityState> authored =
            AuthoredStates(world, schema, package, entities, authorityOnly, *zoneState, stateScope, patches);
        if (!authored.empty())
            zoneState->RecordAuthoredSet(stateScope, std::move(authored));
    }
    std::vector<EntityId> gained;
    if (patches.Store != nullptr)
        MakeGainedMembers(world, partition, patches, package, worldHasTransforms, gained);
    if (zoneState != nullptr && serializers != nullptr && sceneContext != nullptr && IsSimulationAuthority(world))
        MakeHeldRecords(world, partition, *zoneState, stateScope, *serializers, *sceneContext);
    if (patches.Store != nullptr)
        SeedOccupancy(world, *patches.Store, stateScope);

    // The caller's own record of what it made -- which entity is the root, and
    // which are the group a later teardown has to take with it. Only on
    // success: a failed import destroyed everything it created.
    if (created != nullptr)
    {
        created->clear();
        for (const EntityId entity : entities)
            if (entity.IsValid())
                created->push_back(entity);
        created->insert(created->end(), gained.begin(), gained.end());
    }

    if (error != nullptr)
        error->Message.clear();
    return true;
}

bool ImportZonePackageImpl(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    bool publish,
    ZoneParticipation participation,
    ZoneImportError* error,
    ZoneStateScope stateScope)
{
    if (!zone.IsValid())
    {
        SetError(error, "Zone import requires a valid ZoneId.");
        return false;
    }
    if (runtime.FindZone(zone) != nullptr)
    {
        SetError(
            error,
            "Zone package targets an already loaded or importing zone.");
        return false;
    }

    if (stateScope.IsValid() && stateScope.Zone != zone)
    {
        SetError(error, "Zone package's state scope names another zone.");
        return false;
    }

    RuntimeZoneRecord& importing = runtime.BeginZoneImport(zone);
    importing.StateScope = stateScope;
    if (!ImportPackageIntoPartitionImpl(
            runtime.Entities(),
            schema,
            package,
            importing.Partition,
            stateScope,
            serializers,
            sceneContext,
            error,
            nullptr))
    {
        const bool cancelled = runtime.CancelZoneImport(zone);
        (void)cancelled;
        return false;
    }

    if (publish && !runtime.PublishZone(zone, participation))
    {
        const bool cancelled = runtime.CancelZoneImport(zone);
        (void)cancelled;
        SetError(
            error,
            "Zone package could not publish its hidden import partition.");
        return false;
    }

    if (error != nullptr)
        error->Message.clear();
    return true;
}
} // namespace

bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const EntityBuildPackage& package,
    StoragePartitionId partition,
    ZoneStateScope stateScope,
    ZoneImportError* error)
{
    return ImportPackageIntoPartitionImpl(
        world,
        schema,
        package,
        partition,
        stateScope,
        nullptr,
        nullptr,
        error,
        nullptr);
}

bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const EntityBuildPackage& package,
    StoragePartitionId partition,
    ZoneStateScope stateScope,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error,
    std::vector<EntityId>* created)
{
    return ImportPackageIntoPartitionImpl(
        world,
        schema,
        package,
        partition,
        stateScope,
        &serializers,
        &sceneContext,
        error,
        created);
}

bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    ZoneImportError* error,
    ZoneStateScope stateScope)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        zone,
        package,
        nullptr,
        nullptr,
        false,
        ZoneParticipation{},
        error,
        stateScope);
}

bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error,
    ZoneStateScope stateScope)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        zone,
        package,
        &serializers,
        &sceneContext,
        false,
        ZoneParticipation{},
        error,
        stateScope);
}

bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    ZoneParticipation participation,
    ZoneImportError* error,
    ZoneStateScope stateScope)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        zone,
        package,
        nullptr,
        nullptr,
        true,
        participation,
        error,
        stateScope);
}

bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneParticipation participation,
    ZoneImportError* error,
    ZoneStateScope stateScope)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        zone,
        package,
        &serializers,
        &sceneContext,
        true,
        participation,
        error,
        stateScope);
}

bool IsAuthorityMaterialized(const WorldComponentSchema& schema, const PackageEntity& entity)
{
    return std::ranges::any_of(entity.Components, [&](const PackageComponent& component) {
        const WorldComponentSchema::Entry* entry = schema.Find(component.Type);
        return entry != nullptr && entry->AuthorityMaterialized;
    });
}
