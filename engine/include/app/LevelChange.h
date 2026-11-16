#pragma once

#include <app/LevelReference.h>
#include <ecs/EntityId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateStore.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Engine;
class World;

enum class LevelChangeStatus : std::uint8_t
{
    Idle,
    // Accepted; carried out at the next zone-residency phase.
    Pending,
    Loading,
    Resident,
    // Resident, and every participant whose body the change let go has one again.
    BodiesRestored,
    Failed,
};

enum class LevelChangeAdmission : std::uint8_t
{
    Accepted,
    Busy,
    NotCooked,
    NotAuthority,
    InSession,
};

struct LevelChangeRequest
{
    LevelReference Level;
    // A world's starting zone; the manifest's start zone when absent or unknown.
    std::optional<ZoneId> Focus{};
    // What zones remember as the level loads; none is the level as authored.
    std::vector<DurableZoneState> ZoneState{};
};

// docs/gameplay/game-module.md, "Changing level".
class LevelChange
{
public:
    explicit LevelChange(Engine& engine);

    [[nodiscard]] LevelChangeAdmission Request(LevelChangeRequest request);

    [[nodiscard]] LevelChangeStatus Status() const { return Current; }
    // Counts accepted requests, so a caller can tell its own change from a later one.
    [[nodiscard]] std::uint64_t Serial() const { return Accepted; }
    [[nodiscard]] const std::vector<std::string>& Failure() const { return Reasons; }

    // At the zone-residency phase, before streaming: no frame view is live.
    void Commit();
    void Advance();

private:
    void Fail(std::vector<std::string> reasons);
    [[nodiscard]] bool ReleasedHaveBodies(const World& world) const;

    Engine& Host;
    std::optional<LevelChangeRequest> Waiting;
    LevelChangeStatus Current = LevelChangeStatus::Idle;
    std::uint64_t Accepted = 0;
    std::vector<EntityId> Released;
    std::vector<std::string> Reasons;
};
