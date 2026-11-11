#include <line/ContactLineSystem.h>

#include <abilities/AbilityCueLog.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <attributes/AttributeSet.h>
#include <authored/AuthoredNameResolver.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredSchema.h>
#include <authored/AuthoredValueTraits.h>
#include <controller/Aim.h>
#include <core/assets/AssetRef.h>
#include <ecs/World.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <logic/ResponsesSystem.h>
#include <movement/components/HeldMotion.h>
#include <math/geometry/3d/FacingRotation.h>
#include <net/NetSpawnPrefab.h>
#include <physics/CollisionShape.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterMoverLink.h>
#include <projectile/ProjectileHit.h>
#include <projectile/ProjectileSystem.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace
{
// In flight the line stops this far short of its tip, farther than the tip
// travels in a tick: the tip reaches what is ahead of it first, and anything the
// line touches is already behind the tip.
constexpr float kFlightTipGap = 1.0f;
// How far short of its ends the solid test stops, so what the tip is stuck in and
// what stands at the root do not count as across it.
constexpr float kSolidTestInset = 0.25f;
// Ticks in a row something solid may stand across it before it is cut, so a
// corner it only grazes for a moment does not.
constexpr std::uint8_t kBlockedTicksToCut = 2;
constexpr float kShortestLine = 0.05f;
constexpr float kMostRetractSeconds = 3.0f;
// How fast a leash draws its instigator back, in metres per second.
constexpr float kLeashSpeed = 8.0f;
// Marks a tether as a kept line's own, where an ability's tether must win.
constexpr std::uint64_t kLeashKey = 0x4C4C454153484C4Eull;
// Metres down the aim a hand's throw crosses the aim.
constexpr float kHandsConverge = 14.0f;

void BeginRetract(ContactLineState& state)
{
    state.Stage = ContactLineStage::Retracting;
    state.Remaining = kMostRetractSeconds;
}

AuthoredQueryStatus EvaluateAnchored(const ContactLineSystem& system, std::span<const AuthoredValue> arguments,
                                     AuthoredValue& result)
{
    EntityId instigator;
    if (!ReadAuthoredArgument(arguments, 0, instigator))
        return AuthoredQueryStatus::InvalidArguments;
    result = AuthoredValue::Bool(system.AnchorOf(instigator).has_value());
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluateAnchoredWithin(const ContactLineSystem& system, std::span<const AuthoredValue> arguments,
                                           AuthoredValue& result)
{
    EntityId instigator;
    Vec3d point;
    float distance = 0.0f;
    if (!ReadAuthoredArgument(arguments, 0, instigator)
        || !ReadAuthoredArgument(arguments, 1, point)
        || !ReadAuthoredArgument(arguments, 2, distance))
        return AuthoredQueryStatus::InvalidArguments;
    const std::optional<Vec3d> anchor = system.AnchorOf(instigator);
    result = AuthoredValue::Bool(anchor.has_value() && (*anchor - point).Magnitude() <= distance);
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluateAnchor(const ContactLineSystem& system, std::span<const AuthoredValue> arguments,
                                   AuthoredValue& result)
{
    EntityId instigator;
    if (!ReadAuthoredArgument(arguments, 0, instigator))
        return AuthoredQueryStatus::InvalidArguments;
    const std::optional<Vec3d> anchor = system.AnchorOf(instigator);
    if (!anchor)
        return AuthoredQueryStatus::Unavailable;
    result = AuthoredValueTraits<Vec3d>::Encode(*anchor);
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluateStuck(const ContactLineSystem& system, std::span<const AuthoredValue> arguments,
                                  AuthoredValue& result)
{
    EntityId instigator;
    if (!ReadAuthoredArgument(arguments, 0, instigator))
        return AuthoredQueryStatus::InvalidArguments;
    result = AuthoredValue::Bool(system.StuckPoint(instigator, 0.0f).has_value());
    return AuthoredQueryStatus::Value;
}

AuthoredQueryStatus EvaluateStuckPoint(const ContactLineSystem& system, std::span<const AuthoredValue> arguments,
                                       AuthoredValue& result)
{
    EntityId instigator;
    float shortOf = 0.0f;
    if (!ReadAuthoredArgument(arguments, 0, instigator)
        || !ReadAuthoredArgument(arguments, 1, shortOf))
        return AuthoredQueryStatus::InvalidArguments;
    const std::optional<Vec3d> point = system.StuckPoint(instigator, shortOf);
    if (!point)
        return AuthoredQueryStatus::Unavailable;
    result = AuthoredValueTraits<Vec3d>::Encode(*point);
    return AuthoredQueryStatus::Value;
}
} // namespace

void DeclareContactLineVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries)
{
    VerbDefinition extend;
    extend.Name = std::string(kLineExtendLease);
    extend.DisplayName = "Extend line";
    extend.Description = "Throws a line with a projectile and keeps it out while held. Letting go draws it home.";
    extend.Category = "Lines";
    extend.Kind = VerbKind::Lease;
    extend.Completes = true;
    extend.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "instigator", "Instigator", "Whose line it is: where it runs from."));
    DataFieldSchema line = MakeDataField(DataFieldKind::AssetRef, "line", "Line", "The prefab: a scene whose root "
                                                                                 "carries Contact Line.");
    line.Reference.AssetTypeFilter = AssetType::Scene;
    extend.Arguments.Children.push_back(std::move(line));
    DataFieldSchema projectile =
        MakeDataField(DataFieldKind::AssetRef, "projectile", "Projectile", "The projectile prefab that throws its tip.");
    projectile.Reference.AssetTypeFilter = AssetType::Scene;
    extend.Arguments.Children.push_back(std::move(projectile));
    DataFieldSchema at = MakeDataField(DataFieldKind::Vector, "at", "At", "A point to throw it at. The aim when absent.");
    at.VectorLength = 3;
    at.Required = false;
    extend.Arguments.Children.push_back(std::move(at));
    DataFieldSchema keep = MakeDataField(DataFieldKind::Bool, "keep", "Keep",
                                         "Left where it stuck when let go, rather than drawn home.");
    keep.Required = false;
    extend.Arguments.Children.push_back(std::move(keep));
    (void)verbs.Declare(std::move(extend));

    VerbDefinition cut;
    cut.Name = std::string(kLineCutVerb);
    cut.DisplayName = "Cut lines";
    cut.Description = "Ends every line the instigator has out, where it is, striking nothing more.";
    cut.Category = "Lines";
    cut.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "instigator", "Instigator"));
    (void)verbs.Declare(std::move(cut));

    VerbDefinition retract;
    retract.Name = std::string(kLineRetractVerb);
    retract.DisplayName = "Retract lines";
    retract.Description = "Draws home every line the instigator has left anchored.";
    retract.Category = "Lines";
    retract.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "instigator", "Instigator"));
    DataFieldSchema keepNewest = MakeDataField(DataFieldKind::Bool, "keep_newest", "Keep newest",
                                               "Leaves the newest of them where it is.");
    keepNewest.Required = false;
    retract.Arguments.Children.push_back(std::move(keepNewest));
    (void)verbs.Declare(std::move(retract));

    AuthoredQueryDefinition anchored;
    anchored.Name = std::string(kLineAnchoredQuery);
    anchored.DisplayName = "Line anchored";
    anchored.Description = "Whether the instigator is holding a line anchored.";
    anchored.Category = "Lines";
    anchored.Arguments.Children = { AuthoredField<EntityId>("instigator", "Instigator") };
    anchored.Result = AuthoredField<bool>("anchored", "Anchored");
    (void)queries.Declare(std::move(anchored));

    AuthoredQueryDefinition within;
    within.Name = std::string(kLineAnchoredWithinQuery);
    within.DisplayName = "Line anchored within";
    within.Description = "Whether the instigator is holding a line anchored within a distance of a point.";
    within.Category = "Lines";
    within.Arguments.Children = { AuthoredField<EntityId>("instigator", "Instigator"), AuthoredField<Vec3d>("point", "Point"),
                                  AuthoredField<float>("distance", "Distance", "In metres.") };
    within.Result = AuthoredField<bool>("anchored", "Anchored");
    (void)queries.Declare(std::move(within));

    AuthoredQueryDefinition anchor;
    anchor.Name = std::string(kLineAnchorQuery);
    anchor.DisplayName = "Line anchor";
    anchor.Description = "Where the line the instigator is holding is anchored; unavailable when none is.";
    anchor.Category = "Lines";
    anchor.Arguments.Children = { AuthoredField<EntityId>("instigator", "Instigator") };
    anchor.Result = AuthoredField<Vec3d>("anchor", "Anchor");
    (void)queries.Declare(std::move(anchor));

    AuthoredQueryDefinition stuck;
    stuck.Name = std::string(kLineStuckQuery);
    stuck.DisplayName = "Line stuck";
    stuck.Description = "Whether the instigator has let go of a line and left it stuck where it landed.";
    stuck.Category = "Lines";
    stuck.Arguments.Children = { AuthoredField<EntityId>("instigator", "Instigator") };
    stuck.Result = AuthoredField<bool>("stuck", "Stuck");
    (void)queries.Declare(std::move(stuck));

    AuthoredQueryDefinition stuckPoint;
    stuckPoint.Name = std::string(kLineStuckPointQuery);
    stuckPoint.DisplayName = "Line stuck point";
    stuckPoint.Description = "A point on the newest line the instigator has left stuck, some metres short of where "
                             "it landed; unavailable when none is.";
    stuckPoint.Category = "Lines";
    stuckPoint.Arguments.Children = { AuthoredField<EntityId>("instigator", "Instigator"),
                                      AuthoredField<float>("short_of", "Short of", "In metres, back along the line.") };
    stuckPoint.Result = AuthoredField<Vec3d>("point", "Point");
    (void)queries.Declare(std::move(stuckPoint));
}

std::vector<AuthoredQueryBindingToken> BindContactLineQueries(AuthoredQueryDispatcher& dispatcher,
                                                              const ContactLineSystem& system)
{
    const AuthoredQueryRegistry& registry = dispatcher.Registry();
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&EvaluateAnchored>(registry.Find(kLineAnchoredQuery), system));
    tokens.push_back(dispatcher.Bind<&EvaluateAnchoredWithin>(registry.Find(kLineAnchoredWithinQuery), system));
    tokens.push_back(dispatcher.Bind<&EvaluateAnchor>(registry.Find(kLineAnchorQuery), system));
    tokens.push_back(dispatcher.Bind<&EvaluateStuck>(registry.Find(kLineStuckQuery), system));
    tokens.push_back(dispatcher.Bind<&EvaluateStuckPoint>(registry.Find(kLineStuckPointQuery), system));
    return tokens;
}

ContactLineSystem::ContactLineSystem(World& world, const Services& services)
    : Entities(&world)
    , Host(services)
{
    Scratch.reserve(32);
    Overlaps.reserve(16);
}

VerbAdmission ContactLineSystem::Extend(const VerbInvocation& invocation)
{
    World& world = *Entities;
    const auto owned = [&](auto&& visit) {
        if (!world.IsRegistered<ContactLineState>())
            return;
        world.ForEachComponent<ContactLineState>([&](EntityId line, ContactLineState& state) {
            if (state.Owner == invocation.Owner)
                visit(line, state);
        });
    };
    if (invocation.Step == LeaseStep::Probe)
    {
        bool out = false;
        owned([&](EntityId, const ContactLineState& state) { out = out || state.Stage != ContactLineStage::Retracting; });
        return out ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
    if (invocation.Step == LeaseStep::Release)
    {
        Scratch.clear();
        owned([&](EntityId line, ContactLineState& state) {
            state.Owner = {};
            if (state.Stage == ContactLineStage::Extending
                || (state.Kept && state.Stage == ContactLineStage::Anchored))
                return;
            BeginRetract(state);
            const ContactLine* authored = std::as_const(world).TryGet<ContactLine>(line);
            if (authored == nullptr || authored->RetractSpeed <= 0.0f)
                Scratch.push_back(line);
        });
        for (const EntityId line : Scratch)
            Remove(line, invocation.Tick);
        return VerbAdmission::Accepted;
    }

    const AuthoredArguments* arguments = invocation.Arguments;
    EntityId instigator;
    const AssetRef* linePrefab = nullptr;
    const AssetRef* projectilePrefab = nullptr;
    if (arguments == nullptr || !arguments->TryGetEntity(0, instigator) || !arguments->TryGetAsset(1, linePrefab)
        || !arguments->TryGetAsset(2, projectilePrefab) || !world.IsAlive(instigator))
        return VerbAdmission::InvalidArguments;
    if (!IsSimulationAuthority(world) || Host.Prefabs == nullptr || Host.Projectiles == nullptr)
        return VerbAdmission::Refused;
    const AssetId lineId = Host.Projectiles->PrefabOf(*linePrefab);
    const AssetId shotId = Host.Projectiles->PrefabOf(*projectilePrefab);
    if (!lineId.IsValid() || !shotId.IsValid() || Host.Prefabs->Prepare(lineId) != NetPrefabReadiness::Ready)
        return VerbAdmission::UnresolvedReference;

    const Aim aim = AimOf(world, instigator);
    ProjectileLaunch launch{ .Prefab = shotId, .Instigator = instigator, .Origin = aim.Origin,
                             .Direction = aim.Direction, .Tick = invocation.Tick, .SplashScale = 0.0f };
    Vec3d at;
    const bool aimedAt = ReadAuthoredArgument(*arguments, 3, at);
    bool keep = false;
    (void)arguments->TryGetBool(4, keep);

    const EntityId root = Host.Prefabs->Instantiate(lineId, world, world.GetEntityPartition(instigator));
    if (!root.IsValid())
        return VerbAdmission::UnresolvedReference;
    const ContactLine* authored = std::as_const(world).TryGet<ContactLine>(root);
    ContactLineState state;
    state.Owner = invocation.Owner;
    state.Instigator = instigator;
    state.Kept = keep;
    AuthoredNameResolver names(world);
    if (authored == nullptr || !std::isfinite(authored->Radius) || authored->Radius <= 0.0f)
        names.Fail("no positive radius");
    else
    {
        names.Tag(authored->Kind.View(), state.Kind);
        names.Tag(authored->Ignores.View(), state.Ignores);
        names.Tag(authored->AnchorCue.View(), state.AnchorCue);
        names.Tag(authored->CutCue.View(), state.CutCue);
        names.Effect(authored->Effect.View(), state.Effect);
    }
    if (!names.Resolved())
    {
        Host.Prefabs->Despawn(world, root);
        return VerbAdmission::UnresolvedReference;
    }
    if (authored->Slots > 0)
    {
        std::uint32_t taken = 0;
        ContactLineState* oldest = nullptr;
        world.ForEachComponent<ContactLineState>([&](EntityId, ContactLineState& out) {
            if (out.Instigator != instigator || out.Stage == ContactLineStage::Retracting)
                return;
            taken |= 1u << out.Slot;
            if (!out.Owner.IsSet() && (oldest == nullptr || out.Remaining < oldest->Remaining))
                oldest = &out;
        });
        std::uint32_t slot = static_cast<std::uint32_t>(std::countr_one(taken));
        if (slot >= authored->Slots)
        {
            if (oldest == nullptr)
            {
                Host.Prefabs->Despawn(world, root);
                return VerbAdmission::Refused;
            }
            slot = oldest->Slot;
            BeginRetract(*oldest);
        }
        state.Slot = static_cast<std::uint8_t>(slot);
        state.Side = (static_cast<float>(slot) - static_cast<float>(authored->Slots - 1) * 0.5f) * authored->SlotSpacing;
    }
    const Vec3d origin = ContactLineRoot(world, state);
    state.Tip = origin;
    launch.Origin = origin;
    if (aimedAt)
        launch.Direction = at - origin;
    else if (state.Side != 0.0f)
        launch.Direction = aim.Origin + aim.Direction * kHandsConverge - origin;
    launch.MaxDistance = authored->MaxLength;

    launch.Seed = Host.Projectiles->NextSeed(instigator, invocation.Tick);
    ProjectileLaunchResult result = ProjectileLaunchResult::Refused;
    state.Projectile = Host.Projectiles->Launch(launch, &result);
    if (result != ProjectileLaunchResult::Launched)
    {
        Host.Prefabs->Despawn(world, root);
        return result == ProjectileLaunchResult::Full        ? VerbAdmission::QueueFull
             : result == ProjectileLaunchResult::Unresolved ? VerbAdmission::UnresolvedReference
                                                            : VerbAdmission::Refused;
    }

    world.AddComponent<ContactLineState>(root, state);
    PlaceSpawnedRoot(world, root, origin, std::nullopt);
    return VerbAdmission::Accepted;
}

VerbAdmission ContactLineSystem::Cut(const VerbInvocation& invocation)
{
    World& world = *Entities;
    EntityId instigator;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, instigator))
        return VerbAdmission::InvalidArguments;
    if (!IsSimulationAuthority(world))
        return VerbAdmission::Refused;
    if (!world.IsRegistered<ContactLineState>())
        return VerbAdmission::Accepted;
    Scratch.clear();
    world.ForEachComponent<ContactLineState>([&](EntityId line, ContactLineState& state) {
        if (state.Instigator == instigator)
            Scratch.push_back(line);
    });
    for (const EntityId line : Scratch)
    {
        if (const ContactLineState* state = std::as_const(world).TryGet<ContactLineState>(line))
            CueAt(*state, line, state->CutCue, state->Tip, invocation.Tick);
        Remove(line, invocation.Tick);
    }
    return VerbAdmission::Accepted;
}

VerbAdmission ContactLineSystem::Retract(const VerbInvocation& invocation)
{
    EntityId instigator;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, instigator))
        return VerbAdmission::InvalidArguments;
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    bool keepNewest = false;
    (void)invocation.Arguments->TryGetBool(1, keepNewest);
    RetractKeptContactLines(*Entities, instigator, keepNewest);
    return VerbAdmission::Accepted;
}

std::optional<Vec3d> ContactLineSystem::AnchorOf(EntityId instigator) const
{
    const World& world = *Entities;
    std::optional<Vec3d> anchor;
    if (!world.IsRegistered<ContactLineState>())
        return anchor;
    world.ForEachComponent<ContactLineState>([&](EntityId, const ContactLineState& state) {
        if (state.Instigator == instigator && state.Stage == ContactLineStage::Anchored && state.Owner.IsSet())
            anchor = state.Tip;
    });
    return anchor;
}

std::optional<Vec3d> ContactLineSystem::StuckPoint(EntityId instigator, float shortOf) const
{
    const World& world = *Entities;
    if (!world.IsRegistered<ContactLineState>())
        return std::nullopt;
    const ContactLineState* newest = nullptr;
    world.ForEachComponent<ContactLineState>([&](EntityId, const ContactLineState& state) {
        if (state.Instigator == instigator && state.Kept && !state.Owner.IsSet() && state.Stage == ContactLineStage::Anchored
            && (newest == nullptr || state.Remaining > newest->Remaining))
            newest = &state;
    });
    if (newest == nullptr)
        return std::nullopt;
    const Vec3d back = ContactLineRoot(world, *newest) - newest->Tip;
    const float length = back.Magnitude();
    return length > kShortestLine ? newest->Tip + back * (std::min(shortOf, length) / length) : newest->Tip;
}

void ContactLineSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = *Entities;
    if (!world.IsRegistered<ContactLineState>() || !IsSimulationAuthority(world) || Host.Physics == nullptr)
        return;
    if (!Lines)
        Lines.emplace(world);
    Scratch.clear();
    Lines->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            Scratch.push_back(view.Entity(i));
    });
    // In identity order: a strike applies effects, and two lines may strike one target.
    std::vector<EntityId> lines(Scratch);
    std::sort(lines.begin(), lines.end());
    for (const EntityId line : lines)
        if (world.IsAlive(line))
            Step(line, ctx.Time);
    Leash(lines);
}

void ContactLineSystem::Leash(std::span<const EntityId> lines)
{
    World& world = *Entities;
    if (!world.IsRegistered<TetherSession>())
        return;
    Leashes.clear();
    for (const EntityId line : lines)
    {
        if (!world.IsAlive(line))
            continue;
        const ContactLineState* state = std::as_const(world).TryGet<ContactLineState>(line);
        const ContactLine* authored = std::as_const(world).TryGet<ContactLine>(line);
        TetherSession* tether = state != nullptr ? world.TryGet<TetherSession>(state->Instigator) : nullptr;
        if (authored == nullptr || tether == nullptr)
            continue;
        const LeaseOwner owner{ line, kLeashKey };
        if (!state->Kept || state->Owner.IsSet() || state->Stage != ContactLineStage::Anchored || authored->LeashSlack <= 0.0f)
        {
            if (tether->Owner == owner)
                *tether = {};
            continue;
        }
        const LocalTransform* body = std::as_const(world).TryGet<LocalTransform>(state->Instigator);
        if (body == nullptr)
            continue;
        const float allowed = state->Length + authored->LeashSlack;
        const float margin = allowed - (state->Tip - body->Value.Position).Magnitude();
        const auto entry = std::ranges::find(Leashes, state->Instigator, &LeashEntry::Instigator);
        if (entry == Leashes.end())
            Leashes.push_back(LeashEntry{ state->Instigator, line, state->Tip, allowed, margin });
        else if (margin < entry->Margin)
            *entry = LeashEntry{ state->Instigator, line, state->Tip, allowed, margin };
    }
    for (const LeashEntry& entry : Leashes)
    {
        TetherSession* tether = world.TryGet<TetherSession>(entry.Instigator);
        if (tether->Owner.IsSet() && tether->Owner.Key != kLeashKey)
            continue;
        *tether = {};
        tether->Owner = LeaseOwner{ entry.Line, kLeashKey };
        tether->Point = entry.Tip;
        tether->Length = entry.Allowed;
        tether->Speed = kLeashSpeed;
        tether->Yields = true;
    }
}

void ContactLineSystem::Step(EntityId line, const FixedSimTime& now)
{
    World& world = *Entities;
    const ContactLineState* current = std::as_const(world).TryGet<ContactLineState>(line);
    const ContactLine* found = std::as_const(world).TryGet<ContactLine>(line);
    if (current == nullptr || found == nullptr || !world.IsAlive(current->Instigator))
    {
        Remove(line, now.TickIndex);
        return;
    }
    // Copied out of the World: a strike applies effects, and what they do to the
    // World is theirs to decide.
    const ContactLine authored = *found;
    ContactLineState state = *current;
    const float dt = static_cast<float>(now.DeltaSeconds);
    const Vec3d root = ContactLineRoot(world, state);

    const auto retract = [&] { BeginRetract(state); };
    if (state.Stage == ContactLineStage::Extending)
    {
        const ProjectileStops* stops = world.TryGetResource<ProjectileStops>();
        if (const ProjectileStop* stop = stops != nullptr ? stops->Find(state.Projectile) : nullptr)
        {
            state.Tip = stop->Point;
            if (stop->Kind == ProjectileStopKind::Level && (state.Kept || state.Owner.IsSet()))
            {
                state.Stage = ContactLineStage::Anchored;
                state.Remaining = authored.AnchorSeconds;
                if (const LocalTransform* body = std::as_const(world).TryGet<LocalTransform>(state.Instigator))
                    state.Length = (state.Tip - body->Value.Position).Magnitude();
                CueAt(state, line, state.AnchorCue, state.Tip, now.TickIndex);
            }
            else
            {
                // Let go in flight and not meant to stay: nothing is holding it now.
                retract();
            }
        }
        else if (const LocalTransform* tip = world.IsAlive(state.Projectile)
                     ? std::as_const(world).TryGet<LocalTransform>(state.Projectile)
                     : nullptr)
        {
            state.Tip = tip->Value.Position;
        }
        else
        {
            retract();
        }
    }
    else if (state.Stage == ContactLineStage::Anchored)
    {
        state.Remaining -= dt;
        if (state.Remaining <= 0.0f)
            retract();
    }
    else
    {
        state.Remaining -= dt;
        const Vec3d home = root - state.Tip;
        const float distance = home.Magnitude();
        const float step = authored.RetractSpeed * dt;
        if (distance <= step || state.Remaining <= 0.0f)
        {
            Remove(line, now.TickIndex);
            return;
        }
        state.Tip += home * (step / distance);
    }

    if (state.Stage != ContactLineStage::Retracting && !StrikeAlong(line, state, authored, root, now))
    {
        CueAt(state, line, state.CutCue, state.Tip, now.TickIndex);
        Remove(line, now.TickIndex);
        return;
    }

    if (!world.IsAlive(line))
        return;
    ContactLineState* written = world.TryGet<ContactLineState>(line);
    LocalTransform* local = world.TryGet<LocalTransform>(line);
    if (written == nullptr || local == nullptr)
        return;
    // Letting go lands on the record during a strike's effects; never take it back.
    if (written->Stage != ContactLineStage::Retracting || state.Stage == ContactLineStage::Retracting)
        *written = state;
    local->Value.Position = written->Tip;
    const Vec3d along = written->Tip - root;
    if (along.SqrMagnitude() > 1.0e-6f)
        local->Value.Rotation = FacingRotation(along.Normalized());
}

bool ContactLineSystem::StrikeAlong(EntityId line, ContactLineState& state, const ContactLine& authored,
                                    const Vec3d& root, const FixedSimTime& now)
{
    World& world = *Entities;
    const Vec3d delta = state.Tip - root;
    const float length = delta.Magnitude();
    if (length < kShortestLine)
        return true;
    const Vec3d direction = delta / length;
    const PhysicsQueries queries(*Host.Physics);

    const std::array<EntityId, 1> ignored{ state.Instigator };
    const PhysicsQueryFilter solid{ .IncludeTriggers = false,
                                    .IgnoreEntities = ignored,
                                    .IncludeCharacters = false };
    // In flight the tip has just come that way; only a line that is out can have
    // something come between.
    const float clear = length - kSolidTestInset;
    if (state.Stage != ContactLineStage::Extending && clear > kSolidTestInset
        && queries.Raycast(root, direction, clear, solid).Hit)
    {
        // Across a wall it reaches no one, however briefly.
        return ++state.BlockedTicks < kBlockedTicksToCut;
    }
    state.BlockedTicks = 0;

    const float reach = state.Stage == ContactLineStage::Extending ? length - kFlightTipGap : length;
    if (reach <= kShortestLine)
        return true;
    for (std::uint8_t i = 0; i < state.HitCount; ++i)
        state.OffSeconds[i] += static_cast<float>(now.DeltaSeconds);
    const PhysicsQueryFilter touching{ .IncludeTriggers = false,
                                       .IgnoreEntities = ignored,
                                       .IncludeCharacters = true };
    // A capsule's long axis is +Y: a turn from there onto the line.
    const Vec3d up = Vec3d::Up();
    const Vec3d axis = up.Cross(direction);
    const float cosine = std::clamp(up.Dot(direction), -1.0f, 1.0f);
    const Quatf rotation = axis.SqrMagnitude() > 1.0e-8f ? Quatf::FromAxisAngle(axis.Normalized(), std::acos(cosine))
                                                         : Quatf::Identity();
    Overlaps.clear();
    queries.OverlapShape(CollisionShape::MakeCapsule(authored.Radius, std::max(reach * 0.5f, 0.001f)),
                         root + direction * (reach * 0.5f), rotation, Overlaps, touching);

    const bool characters = world.IsRegistered<CharacterMoverLink>();
    for (const EntityId target : Overlaps)
    {
        if (!characters || !world.IsAlive(target) || !world.HasComponent<CharacterMoverLink>(target))
            continue;
        if (state.Ignores.IsValid())
            if (const GameplayTagContainer* tags = std::as_const(world).TryGet<GameplayTagContainer>(target);
                tags != nullptr && tags->HasExact(state.Ignores))
                continue;
        const WorldTransform* pose = std::as_const(world).TryGet<WorldTransform>(target);
        if (pose == nullptr)
            continue;
        // Struck before: only again after staying off it, and touching it restarts that.
        const auto struck = std::find(state.Hits.begin(), state.Hits.begin() + state.HitCount, target);
        const bool known = struck != state.Hits.begin() + state.HitCount;
        if (known)
        {
            float& off = state.OffSeconds[static_cast<std::size_t>(struck - state.Hits.begin())];
            const bool again = authored.RehitSeconds > 0.0f && off >= authored.RehitSeconds;
            off = 0.0f;
            if (!again)
                continue;
        }
        else if (state.HitCount >= kContactLineMaxHits)
        {
            continue;
        }
        const float along = std::clamp((pose->Value.Position - root).Dot(direction), 0.0f, reach);
        const Vec3d point = root + direction * along;
        Vec3d away = pose->Value.Position - point;
        away = away.SqrMagnitude() > 1.0e-6f ? away.Normalized() : direction.Cross(up).Normalized();

        if (!known)
        {
            state.OffSeconds[state.HitCount] = 0.0f;
            state.Hits[state.HitCount++] = target;
        }
        const EffectSource source{ .Instigator = state.Instigator, .Point = point, .Direction = away,
                                   .HasPoint = true, .HasDirection = true };
        if (state.Effect.IsValid() && world.HasComponent<AttributeSet>(target))
            (void)ApplyEffect(world, target, state.Effect, now, LeaseOwner{}, 1.0f, source);
        if (Host.Responses != nullptr)
            (void)Host.Responses->Deliver(target, ProjectileHit{ .Instigator = state.Instigator,
                                                                 .Projectile = line,
                                                                 .Kind = state.Kind,
                                                                 .Point = point,
                                                                 .Normal = away });
    }
    return true;
}

void ContactLineSystem::CueAt(const ContactLineState& state, EntityId line, GameplayTagId cue, const Vec3d& point,
                              std::uint64_t tick)
{
    constexpr std::uint16_t kLineCues = 0x434C;
    AppendSourcedCue(*Entities, state.Instigator, kLineCues, line,
                     AbilityCueEntry{ .Cue = cue, .Tick = tick, .Point = point, .Normal = Vec3d::Zero(), .Target = {} });
}

void ContactLineSystem::Remove(EntityId line, std::uint64_t tick)
{
    World& world = *Entities;
    if (!world.IsAlive(line))
        return;
    if (const ContactLineState* state = std::as_const(world).TryGet<ContactLineState>(line);
        state != nullptr && state->Stage == ContactLineStage::Extending && Host.Projectiles != nullptr)
        Host.Projectiles->Recall(state->Projectile, tick);
    Host.Prefabs != nullptr ? Host.Prefabs->Despawn(world, line) : world.DestroyEntity(line);
}

Vec3d ContactLineRoot(const World& world, const ContactLineState& state)
{
    const Aim aim = AimOf(world, state.Instigator);
    const Vec3d right = aim.Direction.Cross(Vec3d::Up());
    if (state.Side == 0.0f || right.SqrMagnitude() < 1.0e-6f)
        return aim.Origin;
    return aim.Origin + right.Normalized() * state.Side;
}

void CollectContactLines(const World& world, std::vector<ContactLineSegment>& out)
{
    out.clear();
    if (!world.IsRegistered<ContactLineState>())
        return;
    world.ForEachComponent<ContactLineState>([&](EntityId line, const ContactLineState& state) {
        if (state.Stage != ContactLineStage::Retracting && world.IsAlive(state.Instigator))
        {
            const ContactLine* authored = world.TryGet<ContactLine>(line);
            const bool anchored = state.Stage == ContactLineStage::Anchored;
            out.push_back(ContactLineSegment{ state.Instigator, ContactLineRoot(world, state), state.Tip, state.Kept, anchored,
                                              anchored && authored != nullptr ? authored->AnchorSeconds - state.Remaining : 0.0f,
                                              state.Owner.IsSet() });
        }
    });
}

void RetractKeptContactLines(World& world, EntityId instigator, bool keepNewest)
{
    if (!world.IsRegistered<ContactLineState>())
        return;
    const auto stuck = [&](const ContactLineState& state) {
        return state.Instigator == instigator && state.Kept && !state.Owner.IsSet() && state.Stage == ContactLineStage::Anchored;
    };
    float newest = -1.0f;
    if (keepNewest)
        std::as_const(world).ForEachComponent<ContactLineState>([&](EntityId, const ContactLineState& state) {
            if (stuck(state))
                newest = std::max(newest, state.Remaining);
        });
    world.ForEachComponent<ContactLineState>([&](EntityId, ContactLineState& state) {
        if (stuck(state) && !(keepNewest && state.Remaining == newest))
            BeginRetract(state);
    });
}

ContactLineSystem& RegisterContactLineSystem(EngineSchedule& schedule, World& world,
                                             const ContactLineSystem::Services& services)
{
    return schedule.Register<ContactLineSystem>(world, services);
}

void PlaceContactLines(World& world, double presentationAlpha)
{
    if (!world.IsRegistered<ContactLineSpan>())
        return;
    const World& reader = world;
    reader.ForEachComponent<ContactLineSpan>([&](EntityId entity, const ContactLineSpan& span) {
        const Parent* parent = reader.TryGet<Parent>(entity);
        const ContactLineState* state = parent != nullptr ? reader.TryGet<ContactLineState>(parent->Entity) : nullptr;
        LocalTransform* local = const_cast<World&>(reader).TryGet<LocalTransform>(entity);
        if (state == nullptr || local == nullptr || !reader.IsAlive(state->Instigator))
            return;
        const Transform3f tip = PresentationPoseOf(reader, parent->Entity, presentationAlpha);
        const WorldTransform* simulated = reader.TryGet<WorldTransform>(state->Instigator);
        Vec3d root = PresentationPoseOf(reader, state->Instigator, presentationAlpha).Position;
        if (simulated != nullptr)
            root += ContactLineRoot(reader, *state) - simulated->Value.Position;
        const Vec3d back = tip.Rotation.Inverse().RotateVector(root - tip.Position);
        const float length = back.Magnitude();
        if (length < kShortestLine || span.MeshLength <= 0.0f)
        {
            local->Value.Scale.Z = 0.0f;
            return;
        }
        local->Value.Position = back * 0.5f;
        local->Value.Rotation = FacingRotation(back / length);
        local->Value.Scale.Z = length / span.MeshLength;
    });
}
