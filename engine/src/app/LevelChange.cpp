#include <app/LevelChange.h>

#include <app/Engine.h>
#include <app/LoadedLevel.h>
#include <components/ActiveCameraService.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <net/NetSession.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
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

LevelChange::LevelChange(Engine& engine)
    : Host(engine)
{
}

LevelChangeAdmission LevelChange::Request(LevelChangeRequest request)
{
    if (Waiting.has_value() || Current == LevelChangeStatus::Loading)
        return LevelChangeAdmission::Busy;
    if (!IsSimulationAuthority(Host.World().Entities()))
        return LevelChangeAdmission::NotAuthority;
    if (const NetSession* session = Host.TryNet(); session != nullptr && session->Role() != NetSessionRole::Standalone)
        return LevelChangeAdmission::InSession;
    if (!Host.Level().IsCooked(request.Level))
        return LevelChangeAdmission::NotCooked;
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
    for (const EntityId participant : ParticipantsWithBodies(world))
        if (Host.ReleaseParticipantBody(participant).Released)
            Released.push_back(participant);

    LoadedLevel& level = Host.Level();
    level.Unload();
    if (ZoneStateStore* memory = world.TryGetResource<ZoneStateStore>())
        memory->Clear();
    if (ActiveCameraService* cameras = world.TryGetResource<ActiveCameraService>();
        cameras != nullptr && !world.IsAlive(cameras->GetActive()))
    {
        cameras->SetActive({});
    }

    const ConsoleResult loaded = request.Level.Kind == LevelKind::Scene
        ? level.LoadScene(request.Level.Name)
        : level.LoadWorld(request.Level.Name, request.Focus);
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
    Reasons = std::move(reasons);
    Logger& log = Host.Logging().GetLogger<LevelChange>();
    for (const std::string& reason : Reasons)
        log.Error("level change: {}", reason);
}
