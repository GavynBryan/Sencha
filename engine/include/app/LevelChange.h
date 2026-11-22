#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>
#include <participant_spawn/LevelArrival.h>
#include <world/LevelReference.h>
#include <zone/ZoneId.h>
#include <zone/ZoneStateStore.h>

#include <array>
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
    BodyInZone,
};

template<>
struct EnumSchema<LevelChangeAdmission>
{
    static constexpr std::array Values = {
        EnumValue{ LevelChangeAdmission::Accepted, "accepted", "Accepted", "" },
        EnumValue{ LevelChangeAdmission::Busy, "busy", "Busy", "another level change is under way" },
        EnumValue{ LevelChangeAdmission::NotCooked, "not_cooked", "Not cooked", "the level is not cooked" },
        EnumValue{ LevelChangeAdmission::NotAuthority, "not_authority", "Not the authority",
                   "this machine is not the authority" },
        EnumValue{ LevelChangeAdmission::InSession, "in_session", "In a session", "a network session is running" },
        EnumValue{ LevelChangeAdmission::BodyInZone, "body_in_zone", "Body in a zone",
                   "a body to keep belongs to a zone of the level being left" },
    };
};

// The admission's description from its schema.
[[nodiscard]] const char* LevelChangeAdmissionReason(LevelChangeAdmission admission);

// What a level change keeps of the level it enters.
enum class LevelMemory : std::uint8_t
{
    // As the game left it: what its zones remember applies.
    Remembered,
    // As content made it: its memory and the records waiting in it are forgotten first.
    Authored,
};

// What becomes of the participants' bodies.
enum class LevelChangeBodies : std::uint8_t
{
    // Released, and the game's prefab spawned in the new level.
    Fresh,
    // The same bodies, held still while no level stands and placed when the new one arrives.
    Kept,
};

enum class LevelChangeKind : std::uint8_t
{
    // The game goes on, in another level or this one again.
    Travel,
    // Every level's memory, every runtime held record and every unclaimed section goes.
    NewGame,
    // What every level remembers is replaced; bodies come from the save.
    Restore,
};

// Built only through its named constructors: docs/gameplay/game-module.md, "Changing level".
class LevelChangeRequest
{
public:
    [[nodiscard]] static LevelChangeRequest Travel(LevelReference level, LevelMemory memory,
                                                   LevelChangeBodies bodies);
    [[nodiscard]] static LevelChangeRequest NewGame(LevelReference level);
    [[nodiscard]] static LevelChangeRequest Restore(LevelReference level, std::vector<DurableLevelState> remembered);

    // Where a body arrives; the engine carries the name and does not read it.
    LevelChangeRequest& At(std::string arrival);
    // A world's starting zone; the manifest's start zone when absent or unknown.
    LevelChangeRequest& Focused(ZoneId zone);

    [[nodiscard]] LevelChangeKind Kind() const { return Kind_; }
    [[nodiscard]] const LevelReference& Level() const { return Level_; }
    [[nodiscard]] LevelMemory Memory() const { return Memory_; }
    [[nodiscard]] LevelChangeBodies Bodies() const { return Bodies_; }
    [[nodiscard]] const std::optional<ZoneId>& Focus() const { return Focus_; }
    [[nodiscard]] const std::string& Arrival() const { return Arrival_; }
    [[nodiscard]] const std::vector<DurableLevelState>& Remembered() const { return Remembered_; }

private:
    LevelChangeRequest(LevelChangeKind kind, LevelReference level, LevelMemory memory);

    LevelChangeKind Kind_;
    LevelReference Level_;
    LevelMemory Memory_;
    LevelChangeBodies Bodies_ = LevelChangeBodies::Fresh;
    std::optional<ZoneId> Focus_;
    std::string Arrival_;
    std::vector<DurableLevelState> Remembered_;
};

// docs/gameplay/game-module.md, "Changing level". The first load, with nothing
// loaded, is admitted on any machine and in a session: it changes nothing a
// peer was told.
class LevelChange
{
public:
    explicit LevelChange(Engine& engine);

    [[nodiscard]] LevelChangeAdmission Request(LevelChangeRequest request);

    [[nodiscard]] LevelChangeStatus Status() const { return Current; }
    // Counts accepted requests, so a caller can tell its own change from a later one.
    [[nodiscard]] std::uint64_t Serial() const { return Accepted; }
    // Of the latest accepted request.
    [[nodiscard]] LevelChangeKind Kind() const { return LatestKind; }
    [[nodiscard]] const std::vector<std::string>& Failure() const { return Reasons; }

    // At the zone-residency phase, before streaming: no frame view is live.
    void Commit();
    void Advance();

private:
    void Fail(std::vector<std::string> reasons);
    static void ForgetFor(World& world, const LevelChangeRequest& request,
                          const std::optional<LevelReference>& leaving);
    [[nodiscard]] std::vector<KeptArrival> HoldKeptBodies(World& world);
    void PlaceKeptBodies(World& world);
    void LetGoOfKeptBodies(World& world);
    void MoveBody(World& world, EntityId body, const Transform3f& place);
    [[nodiscard]] std::uint64_t LastTick() const;
    [[nodiscard]] bool BodiesArrived(const World& world) const;

    Engine& Host;
    std::optional<LevelChangeRequest> Waiting;
    LevelChangeStatus Current = LevelChangeStatus::Idle;
    std::uint64_t Accepted = 0;
    LevelChangeKind LatestKind = LevelChangeKind::Travel;
    std::vector<EntityId> Released;
    std::vector<std::string> Reasons;
};
