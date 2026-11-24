#include <app/LevelChange.h>

#include <app/Engine.h>
#include <app/LoadedLevel.h>
#include <components/ActiveCameraService.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <movement/components/CharacterFacts.h>
#include <net/NetSession.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/RestoredBodies.h>
#include <physics/PhysicsStepSystem.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecords.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/ZoneStateStore.h>

#include <utility>

namespace
{
    [[nodiscard]] std::vector<EntityId> ParticipantsWithBodies(const World& world)
    {
        std::vector<EntityId> participants;
        if (!world.IsRegistered<ParticipantControl>())
            return participants;
        world.ForEachComponent<ParticipantControl>([&](EntityId participant, const ParticipantControl& control) {
            if (world.IsAlive(control.Body))
                participants.push_back(participant);
        });
        return participants;
    }

    constexpr std::uint64_t kArrivalHoldKey = 1;

    [[nodiscard]] LeaseOwner ArrivalHoldOf(EntityId participant)
    {
        return LeaseOwner{ .Entity = participant, .Key = kArrivalHoldKey };
    }

    [[nodiscard]] bool HoldStill(World& world, EntityId participant, EntityId body, std::uint64_t tick)
    {
        if (!world.IsRegistered<ActorTimeLeases>() || !world.IsRegistered<ActorTime>()
            || !world.HasComponent<ActorTime>(body))
            return true;
        if (!world.HasComponent<ActorTimeLeases>(body))
            world.AddComponent<ActorTimeLeases>(body, ActorTimeLeases{});
        return AcquireActorTimeLease(*world.TryGet<ActorTimeLeases>(body), ArrivalHoldOf(participant), 0, tick);
    }

    void LetGo(World& world, EntityId participant, EntityId body, std::uint64_t tick)
    {
        if (ActorTimeLeases* leases = world.IsRegistered<ActorTimeLeases>() && world.IsAlive(body)
                ? world.TryGet<ActorTimeLeases>(body)
                : nullptr)
            ReleaseActorTimeLeases(*leases, ArrivalHoldOf(participant), tick);
    }
}

const char* LevelChangeAdmissionReason(LevelChangeAdmission admission)
{
    const EnumValue<LevelChangeAdmission>* value = FindEnumValue(admission);
    return value != nullptr ? value->Tooltip.data() : "";
}

LevelChangeRequest::LevelChangeRequest(LevelChangeKind kind, LevelReference level, LevelMemory memory)
    : Kind_(kind)
    , Level_(std::move(level))
    , Memory_(memory)
{
}

LevelChangeRequest LevelChangeRequest::Travel(LevelReference level, LevelMemory memory, LevelChangeBodies bodies)
{
    LevelChangeRequest request(LevelChangeKind::Travel, std::move(level), memory);
    request.Bodies_ = bodies;
    return request;
}

LevelChangeRequest LevelChangeRequest::NewGame(LevelReference level)
{
    return LevelChangeRequest(LevelChangeKind::NewGame, std::move(level), LevelMemory::Authored);
}

LevelChangeRequest LevelChangeRequest::Restore(LevelReference level, std::vector<DurableLevelState> remembered)
{
    LevelChangeRequest request(LevelChangeKind::Restore, std::move(level), LevelMemory::Remembered);
    request.Remembered_ = std::move(remembered);
    return request;
}

LevelChangeRequest& LevelChangeRequest::At(std::string arrival)
{
    Arrival_ = std::move(arrival);
    return *this;
}

LevelChangeRequest& LevelChangeRequest::Focused(ZoneId zone)
{
    Focus_ = zone;
    return *this;
}

LevelChange::LevelChange(Engine& engine)
    : Host(engine)
{
}

LevelChangeAdmission LevelChange::Request(LevelChangeRequest request)
{
    if (Waiting.has_value() || Current == LevelChangeStatus::Loading)
        return LevelChangeAdmission::Busy;
    const World& world = Host.World().Entities();
    if (request.Bodies() == LevelChangeBodies::Kept)
        for (const EntityId participant : ParticipantsWithBodies(world))
            if (world.GetEntityPartition(LivingBodyOf(world, participant)) != PersistentStoragePartition)
                return LevelChangeAdmission::BodyInZone;
    const bool firstLoad = !Host.Level().Loaded().has_value();
    if (!firstLoad && !IsSimulationAuthority(world))
        return LevelChangeAdmission::NotAuthority;
    if (const NetSession* session = Host.TryNet();
        !firstLoad && session != nullptr && session->Role() != NetSessionRole::Standalone)
        return LevelChangeAdmission::InSession;
    if (!Host.Level().IsCooked(request.Level()))
        return LevelChangeAdmission::NotCooked;
    LatestKind = request.Kind();
    Waiting = std::move(request);
    Current = LevelChangeStatus::Pending;
    Reasons.clear();
    ++Accepted;
    return LevelChangeAdmission::Accepted;
}

void LevelChange::Commit()
{
    if (!Waiting.has_value())
        return;
    const LevelChangeRequest request = std::move(*Waiting);
    Waiting.reset();

    World& world = Host.World().Entities();
    LetGoOfKeptBodies(world);
    Released.clear();
    std::vector<KeptArrival> kept;
    if (request.Bodies() == LevelChangeBodies::Kept)
        kept = HoldKeptBodies(world);
    else if (IsSimulationAuthority(world))
        for (const EntityId participant : ParticipantsWithBodies(world))
            if (Host.ReleaseParticipantBody(participant).Released)
                Released.push_back(participant);

    LoadedLevel& level = Host.Level();
    const std::optional<LevelReference> leaving = level.Loaded();
    level.Unload(Forgets(request, leaving) ? UnloadedState::Forgotten : UnloadedState::Captured);
    ForgetFor(world, request, leaving);
    world.SetResource(LevelArrival{ .Name = request.Arrival(), .Spawning = Released, .Kept = std::move(kept) });
    if (RestoredBodies* restored = world.TryGetResource<RestoredBodies>())
        *restored = RestoredBodies{};
    if (ActiveCameraService* cameras = world.TryGetResource<ActiveCameraService>();
        cameras != nullptr && !world.IsAlive(cameras->GetActive()))
    {
        cameras->SetActive({});
    }

    const ConsoleResult loaded = request.Level().Kind == LevelKind::Scene
        ? level.LoadScene(request.Level().Name)
        : level.LoadWorld(request.Level().Name, request.Focus());
    if (!loaded.Succeeded())
    {
        std::vector<std::string> reasons;
        for (const ConsoleOutputEntry& line : loaded.Output)
            reasons.push_back(line.Text);
        Fail(std::move(reasons));
        return;
    }
    Current = LevelChangeStatus::Loading;
}

void LevelChange::Advance()
{
    if (Current == LevelChangeStatus::Loading)
    {
        switch (Host.Level().Residency())
        {
        case LevelResidency::Resident:
            Current = LevelChangeStatus::Resident;
            break;
        case LevelResidency::Failed:
            Fail({ "the level's zone failed to load; the zone loader logged why" });
            return;
        case LevelResidency::Unloaded:
            Fail({ "something unloaded the level before it arrived" });
            return;
        case LevelResidency::Loading:
            return;
        }
    }
    if (Current != LevelChangeStatus::Resident)
        return;
    World& world = Host.World().Entities();
    PlaceKeptBodies(world);
    if (BodiesArrived(world))
        Current = LevelChangeStatus::BodiesRestored;
}

std::uint64_t LevelChange::LastTick() const
{
    const std::uint64_t next = Host.Runtime().GetSimulationClock().GetTickIndex();
    return next > 0 ? next - 1 : 0;
}

std::vector<KeptArrival> LevelChange::HoldKeptBodies(World& world)
{
    std::vector<KeptArrival> kept;
    if (!IsSimulationAuthority(world))
        return kept;
    const std::uint64_t tick = LastTick();
    for (const EntityId participant : ParticipantsWithBodies(world))
    {
        if (!HoldStill(world, participant, LivingBodyOf(world, participant), tick))
            Host.Logging().GetLogger<LevelChange>().Warn(
                "level change: a kept body has every time lease in use, so it is not held still until it arrives");
        kept.push_back(KeptArrival{ .Participant = participant, .Place = std::nullopt });
    }
    return kept;
}

void LevelChange::PlaceKeptBodies(World& world)
{
    LevelArrival* arrival = world.TryGetResource<LevelArrival>();
    if (arrival == nullptr)
        return;
    const std::uint64_t tick = LastTick();
    std::erase_if(arrival->Kept, [&](const KeptArrival& kept) {
        const EntityId body = LivingBodyOf(world, kept.Participant);
        if (!body.IsValid())
            return true;
        if (!kept.Place.has_value())
            return false;
        MoveBody(world, body, *kept.Place);
        LetGo(world, kept.Participant, body, tick);
        return true;
    });
}

void LevelChange::LetGoOfKeptBodies(World& world)
{
    LevelArrival* arrival = world.TryGetResource<LevelArrival>();
    if (arrival == nullptr)
        return;
    const std::uint64_t tick = LastTick();
    for (const KeptArrival& kept : arrival->Kept)
        LetGo(world, kept.Participant, LivingBodyOf(world, kept.Participant), tick);
    arrival->Kept.clear();
}

void LevelChange::MoveBody(World& world, EntityId body, const Transform3f& place)
{
    PhysicsStepSystem* physics = Host.Schedule().Get<PhysicsStepSystem>();
    const bool moved = physics != nullptr && physics->GetCharacterMovers().SetPosition(world, body, place.Position);
    if (LocalTransform* local = world.TryGet<LocalTransform>(body))
    {
        local->Value.Position = place.Position;
        local->Value.Rotation = place.Rotation;
    }
    if (!moved)
        RequestTransformHistorySnap(world, body);
    if (KinematicState* motion = world.TryGet<KinematicState>(body))
        motion->Velocity = Vec3d::Zero();
}

bool LevelChange::Forgets(const LevelChangeRequest& request, const std::optional<LevelReference>& leaving)
{
    switch (request.Kind())
    {
    case LevelChangeKind::Travel:
        return request.Memory() == LevelMemory::Authored && leaving.has_value() && *leaving == request.Level();
    case LevelChangeKind::NewGame:
    case LevelChangeKind::Restore:
        return true;
    }
    return false;
}

void LevelChange::ForgetFor(World& world, const LevelChangeRequest& request,
                            const std::optional<LevelReference>& leaving)
{
    ZoneStateStore* memory = world.TryGetResource<ZoneStateStore>();
    const auto levelOf = [memory](const LevelReference& level) {
        return memory != nullptr ? memory->Level(level) : ZoneStateLevelId{};
    };
    if (leaving.has_value())
        SetAsideHeldRecords(world, levelOf(*leaving));
    switch (request.Kind())
    {
    case LevelChangeKind::Travel:
        if (request.Memory() == LevelMemory::Remembered)
            return ResumeHeldRecords(world, levelOf(request.Level()));
        if (memory != nullptr)
            memory->ForgetLevel(request.Level());
        return DestroySetAsideHeldRecords(world, levelOf(request.Level()));
    case LevelChangeKind::NewGame:
        if (memory != nullptr)
            memory->Clear();
        return DestroyHeldRecords(world);
    case LevelChangeKind::Restore:
        if (memory != nullptr)
            memory->Install(request.Remembered());
        return DestroyHeldRecords(world);
    }
}

bool LevelChange::BodiesArrived(const World& world) const
{
    if (const LevelArrival* arrival = world.TryGetResource<LevelArrival>(); arrival != nullptr && !arrival->Kept.empty())
        return false;
    for (const EntityId participant : Released)
    {
        if (!world.IsAlive(participant))
            continue;
        const ParticipantControl* control = world.TryGet<ParticipantControl>(participant);
        if (control == nullptr || !world.IsAlive(control->Body))
            return false;
    }
    return true;
}

void LevelChange::Fail(std::vector<std::string> reasons)
{
    Current = LevelChangeStatus::Failed;
    World& world = Host.World().Entities();
    LetGoOfKeptBodies(world);
    if (RestoredBodies* restored = world.TryGetResource<RestoredBodies>())
        restored->Local.reset();
    Reasons = std::move(reasons);
    Logger& log = Host.Logging().GetLogger<LevelChange>();
    for (const std::string& reason : Reasons)
        log.Error("level change: {}", reason);
}
