#include <app/LevelChange.h>

#include <app/Engine.h>
#include <app/LoadedLevel.h>
#include <components/ActiveCameraService.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <net/NetSession.h>
#include <participant/ParticipantControl.h>
#include <participant_spawn/RestoredBodies.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecords.h>
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
}

const char* LevelChangeAdmissionReason(LevelChangeAdmission admission)
{
    switch (admission)
    {
    case LevelChangeAdmission::Accepted:
        return "";
    case LevelChangeAdmission::Busy:
        return "another level change is under way";
    case LevelChangeAdmission::NotCooked:
        return "the level is not cooked";
    case LevelChangeAdmission::NotAuthority:
        return "this machine is not the authority";
    case LevelChangeAdmission::InSession:
        return "a network session is running";
    }
    return "";
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
    const bool firstLoad = !Host.Level().Loaded().has_value();
    if (!firstLoad && !IsSimulationAuthority(Host.World().Entities()))
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
    Released.clear();
    if (IsSimulationAuthority(world))
        for (const EntityId participant : ParticipantsWithBodies(world))
            if (Host.ReleaseParticipantBody(participant).Released)
                Released.push_back(participant);

    LoadedLevel& level = Host.Level();
    const std::optional<LevelReference> leaving = level.Loaded();
    level.Unload();
    ForgetFor(world, request, leaving);
    world.SetResource(LevelArrival{ .Name = request.Arrival() });
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
    if (Current == LevelChangeStatus::Resident && ReleasedHaveBodies(Host.World().Entities()))
        Current = LevelChangeStatus::BodiesRestored;
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

bool LevelChange::ReleasedHaveBodies(const World& world) const
{
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
    if (RestoredBodies* restored = Host.World().Entities().TryGetResource<RestoredBodies>())
        restored->Local.reset();
    Reasons = std::move(reasons);
    Logger& log = Host.Logging().GetLogger<LevelChange>();
    for (const std::string& reason : Reasons)
        log.Error("level change: {}", reason);
}
