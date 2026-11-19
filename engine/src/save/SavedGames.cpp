#include <save/SavedGames.h>

#include <app/Engine.h>
#include <app/LevelChange.h>
#include <app/LoadedLevel.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <net/NetSession.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentIdMint.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace
{
    struct ReadOutcome
    {
        std::optional<SavedGame> Saved;
        std::string Error;
        // The slot's file did not read, and this is the backup beside it.
        bool FromBackup = false;
    };

    // A file a newer build wrote is not passed over for an older backup.
    ReadOutcome ReadSlot(const SaveSlotFiles& files, const std::string& slot)
    {
        ReadOutcome outcome;
        outcome.Error = "no save in slot '" + slot + "'";
        if (const std::optional<std::vector<std::byte>> current = files.Read(slot))
        {
            DecodedSavedGame decoded = DecodeSavedGame(*current);
            outcome.Saved = std::move(decoded.Saved);
            if (outcome.Saved.has_value())
                return outcome;
            outcome.Error = std::move(decoded.Error);
            if (decoded.Newer)
                return outcome;
        }
        if (const std::optional<std::vector<std::byte>> backup = files.ReadBackup(slot))
        {
            DecodedSavedGame decoded = DecodeSavedGame(*backup);
            outcome.Saved = std::move(decoded.Saved);
            outcome.FromBackup = outcome.Saved.has_value();
            if (decoded.Newer)
                outcome.Error = std::move(decoded.Error);
        }
        return outcome;
    }

    // The file being replaced becomes the backup unless it is corrupt, so a
    // corrupt file never takes the place of a good backup.
    bool WriteToDisk(const SaveSlotFiles& files, const std::string& slot, const SavedGame& saved)
    {
        const std::vector<std::byte> bytes = EncodeSavedGame(saved);
        if (bytes.empty())
            return false;
        if (const std::optional<std::vector<std::byte>> current = files.Read(slot))
            if (const DecodedSavedGame decoded = DecodeSavedGame(*current); decoded.Saved.has_value() || decoded.Newer)
                (void)files.WriteBackup(slot, *current);
        return files.Write(slot, bytes);
    }

    bool LevelChanging(LevelChangeStatus status)
    {
        return status == LevelChangeStatus::Pending || status == LevelChangeStatus::Loading
            || status == LevelChangeStatus::Resident;
    }

    std::string LevelChangeFailure(const LevelChange& changes)
    {
        return changes.Failure().empty() ? std::string("the level change failed")
                                         : "the level change failed: " + changes.Failure().front();
    }

    SavedGameSection CapturePersistentIds(const World& world)
    {
        const PersistentIdMint* mint = world.TryGetResource<PersistentIdMint>();
        return SavedGameSection{ .Name = std::string(kPersistentIdsSection),
                                 .Version = 1,
                                 .Data = JsonValue(JsonValue::Object{
                                     { "next", JsonValue(static_cast<double>(mint != nullptr ? mint->Next : 1)) } }) };
    }

    void RestorePersistentIds(World& world, const JsonValue& data)
    {
        const auto next = static_cast<std::uint64_t>(data.NumberOr("next", 1.0));
        PersistentIdMint* mint = world.TryGetResource<PersistentIdMint>();
        if (mint == nullptr)
            mint = &world.AddResource<PersistentIdMint>();
        mint->Next = std::max(mint->Next, next);
    }

    const char* AdmissionReason(LevelChangeAdmission admission)
    {
        switch (admission)
        {
        case LevelChangeAdmission::Accepted:
            return "";
        case LevelChangeAdmission::Busy:
            return "another level change is under way";
        case LevelChangeAdmission::NotCooked:
            return "the saved level is not cooked";
        case LevelChangeAdmission::NotAuthority:
            return "this machine is not the authority";
        case LevelChangeAdmission::InSession:
            return "a network session is running";
        }
        return "";
    }
}

SavedGames::SavedGames(Engine& engine, std::filesystem::path root)
    : Host(engine)
{
    if (!root.empty())
        Files.emplace(std::move(root));
}

SavedGames::~SavedGames()
{
    if (InFlight.has_value())
        (void)Host.Tasks().Cancel(InFlight->Task);
    if (ReadTask.IsValid())
        (void)Host.Tasks().Cancel(ReadTask);
}

bool SavedGames::RegisterSection(SavedGameSectionDefinition section)
{
    if (section.Name.empty() || section.Name == kPersistentIdsSection
        || std::ranges::find(Sections, section.Name, &SavedGameSectionDefinition::Name) != Sections.end())
        return false;
    std::erase_if(Unclaimed, [&](const SavedGameSection& unclaimed) { return unclaimed.Name == section.Name; });
    Sections.push_back(std::move(section));
    return true;
}

void SavedGames::UnregisterSection(std::string_view name)
{
    std::erase_if(Sections, [&](const SavedGameSectionDefinition& section) { return section.Name == name; });
}

SaveAdmission SavedGames::RequestWrite(std::string slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return SaveAdmission::BadSlot;
    if (WriteWaiting.has_value() || Writing == SaveStatus::Working)
        return SaveAdmission::Busy;
    if (!IsSimulationAuthority(Host.World().Entities()))
        return SaveAdmission::NotAuthority;
    if (!Host.Level().Loaded().has_value() && !LevelChanging(Host.LevelChanges().Status()))
        return SaveAdmission::NothingLoaded;
    WriteWaiting = std::move(slot);
    Writing = SaveStatus::Pending;
    return SaveAdmission::Accepted;
}

SaveAdmission SavedGames::RequestRestore(std::string slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return SaveAdmission::BadSlot;
    if (RestoreWaiting.has_value() || Restoring == RestoreProgress::Reading || Restoring == RestoreProgress::Loading)
        return SaveAdmission::Busy;
    if (!IsSimulationAuthority(Host.World().Entities()))
        return SaveAdmission::NotAuthority;
    if (const NetSession* session = Host.TryNet(); session != nullptr && session->Role() != NetSessionRole::Standalone)
        return SaveAdmission::InSession;
    RestoreWaiting = std::move(slot);
    Restoring = RestoreProgress::Pending;
    RestoreReason.clear();
    FromBackup = false;
    ++RestoreCount;
    return SaveAdmission::Accepted;
}

bool SavedGames::Holds(std::string_view slot) const
{
    if (Remembered.contains(slot))
        return true;
    return Files.has_value() && SaveSlotFiles::IsSlotName(slot)
        && ReadSlot(*Files, std::string(slot)).Saved.has_value();
}

void SavedGames::Commit()
{
    if (WriteWaiting.has_value())
    {
        const std::optional<std::string> refused = WriteRefusal();
        if (refused.has_value() && refused->empty())
            return CommitRestore();
        const std::string slot = std::move(*WriteWaiting);
        WriteWaiting.reset();
        if (refused.has_value())
            FailWrite(*refused);
        else if (const std::shared_ptr<const SavedGame> saved = Capture(slot))
            WriteOnTasks(slot, saved);
    }
    CommitRestore();
}

void SavedGames::CommitRestore()
{
    if (RestoreWaiting.has_value())
    {
        const std::string slot = std::move(*RestoreWaiting);
        RestoreWaiting.reset();
        Restore(slot);
    }
}

std::optional<std::string> SavedGames::WriteRefusal() const
{
    const LevelChangeStatus change = Host.LevelChanges().Status();
    if (LevelChanging(change))
        return std::string();
    if (change == LevelChangeStatus::Failed)
        return std::string("the level change it waited for failed");
    switch (Host.Level().Residency())
    {
    case LevelResidency::Resident:
        return std::nullopt;
    case LevelResidency::Loading:
        return std::string();
    case LevelResidency::Unloaded:
        return std::string("nothing is loaded");
    case LevelResidency::Failed:
        return std::string("the level failed to load");
    }
    return std::string("nothing is loaded");
}

std::shared_ptr<const SavedGame> SavedGames::Capture(const std::string& slot)
{
    LoadedLevel& level = Host.Level();
    level.CaptureState();

    World& world = Host.World().Entities();
    auto saved = std::make_shared<SavedGame>();
    saved->Summary.Level = *level.Loaded();
    if (saved->Summary.Level.Kind == LevelKind::World && level.PlayZone().IsValid())
        saved->Summary.Focus = level.PlayZone();
    saved->Summary.SavedAtUnixSeconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    if (const ZoneStateStore* store = world.TryGetResource<ZoneStateStore>())
        saved->Zones = store->ExportDurable();
    for (const SavedGameSectionDefinition& section : Sections)
        if (section.Capture)
            saved->Sections.push_back(SavedGameSection{ section.Name, section.Version, section.Capture(world) });
    saved->Sections.insert(saved->Sections.end(), Unclaimed.begin(), Unclaimed.end());
    saved->Sections.push_back(CapturePersistentIds(world));
    Remembered.insert_or_assign(slot, saved);
    if (!Files.has_value())
    {
        Writing = SaveStatus::Done;
        return nullptr;
    }
    return saved;
}

void SavedGames::WriteOnTasks(const std::string& slot, std::shared_ptr<const SavedGame> saved)
{
    Writing = SaveStatus::Working;
    InFlight = InFlightWrite{ .Slot = slot, .Saved = saved, .Task = {} };
    InFlight->Task = Host.Tasks().Submit<bool>(
        [files = *Files, slot, saved] { return WriteToDisk(files, slot, *saved); },
        [this](bool written) { FinishWrite(written); });
}

void SavedGames::FinishWrite(bool written)
{
    const std::string slot = std::move(InFlight->Slot);
    InFlight.reset();
    if (written)
        Writing = SaveStatus::Done;
    else
        FailWrite("could not write '" + Files->PathOf(slot).string() + "'");
}

void SavedGames::Finish()
{
    if (WriteWaiting.has_value())
    {
        const std::optional<std::string> refused = WriteRefusal();
        const std::string slot = std::move(*WriteWaiting);
        WriteWaiting.reset();
        if (refused.has_value())
            return FailWrite(refused->empty() ? "the game quit while the level was changing" : *refused);
        if (const std::shared_ptr<const SavedGame> saved = Capture(slot))
        {
            InFlight = InFlightWrite{ .Slot = slot, .Saved = saved, .Task = {} };
            FinishWrite(WriteToDisk(*Files, slot, *saved));
        }
        return;
    }
    if (InFlight.has_value() && Host.Tasks().GetState(InFlight->Task) == AsyncTaskState::Pending
        && Host.Tasks().Cancel(InFlight->Task))
        FinishWrite(WriteToDisk(*Files, InFlight->Slot, *InFlight->Saved));
}

void SavedGames::Restore(const std::string& slot)
{
    if (const auto remembered = Remembered.find(slot); remembered != Remembered.end())
        return BeginChange(remembered->second);
    if (!Files.has_value())
        return FailRestore("no save in slot '" + slot + "'");
    Restoring = RestoreProgress::Reading;
    ReadTask = Host.Tasks().Submit<ReadOutcome>([files = *Files, slot] { return ReadSlot(files, slot); },
                                                [this, slot](ReadOutcome outcome) {
                                                    ReadTask = {};
                                                    if (!outcome.Saved.has_value())
                                                        return FailRestore(std::move(outcome.Error));
                                                    if (outcome.FromBackup)
                                                        Host.Logging().GetLogger<SavedGames>().Warn(
                                                            "restore: slot '{}' did not read ({}); restoring its backup",
                                                            slot, outcome.Error);
                                                    FromBackup = outcome.FromBackup;
                                                    const auto saved =
                                                        std::make_shared<const SavedGame>(std::move(*outcome.Saved));
                                                    Remembered.insert_or_assign(slot, saved);
                                                    BeginChange(saved);
                                                });
}

void SavedGames::BeginChange(std::shared_ptr<const SavedGame> saved)
{
    for (const SavedGameSection& section : saved->Sections)
        if (const auto registered = std::ranges::find(Sections, section.Name, &SavedGameSectionDefinition::Name);
            registered != Sections.end() && section.Version > registered->Version)
            return FailRestore("section '" + section.Name + "' was written by a newer build (version "
                               + std::to_string(section.Version) + "; this build reads up to "
                               + std::to_string(registered->Version) + ")");
    const LevelChangeAdmission admission = Host.LevelChanges().Request(LevelChangeRequest{
        .Level = saved->Summary.Level, .Focus = saved->Summary.Focus, .ZoneState = saved->Zones });
    if (admission != LevelChangeAdmission::Accepted)
        return FailRestore(AdmissionReason(admission));
    RestoreSerial = Host.LevelChanges().Serial();
    SectionsWaiting = std::move(saved);
    Restoring = RestoreProgress::Loading;
}

void SavedGames::RestoreSections()
{
    const LevelChange& changes = Host.LevelChanges();
    if (changes.Serial() != SeenChange)
    {
        SeenChange = changes.Serial();
        if (SeenChange != RestoreSerial && changes.RuntimeState() == LevelChangeRuntimeState::Discard)
            Unclaimed.clear();
    }
    if (SectionsWaiting == nullptr || changes.Serial() != RestoreSerial)
        return;
    const LevelChangeStatus change = changes.Status();
    if (change == LevelChangeStatus::Pending)
        return;
    const std::shared_ptr<const SavedGame> saved = std::move(SectionsWaiting);
    if (change == LevelChangeStatus::Failed)
        return FailRestore(LevelChangeFailure(changes));

    World& world = Host.World().Entities();
    Logger& log = Host.Logging().GetLogger<SavedGames>();
    Unclaimed.clear();
    const JsonValue none;
    for (const SavedGameSection& section : saved->Sections)
    {
        if (section.Name == kPersistentIdsSection)
            RestorePersistentIds(world, section.Data);
        else if (std::ranges::find(Sections, section.Name, &SavedGameSectionDefinition::Name) == Sections.end())
            Unclaimed.push_back(section);
    }
    for (const SavedGameSectionDefinition& registered : Sections)
    {
        const auto found = std::ranges::find(saved->Sections, registered.Name, &SavedGameSection::Name);
        if (!registered.Restore)
            continue;
        if (found == saved->Sections.end())
            registered.Restore(world, registered.Version, none);
        else if (found->Version <= registered.Version)
            registered.Restore(world, found->Version, found->Data);
        else
            log.Error("restore: section '{}' is version {}, newer than this build's {}; it was not restored",
                      registered.Name, found->Version, registered.Version);
    }
}

void SavedGames::Advance()
{
    if (Restoring != RestoreProgress::Loading)
        return;
    const LevelChange& changes = Host.LevelChanges();
    if (changes.Serial() != RestoreSerial)
        return FailRestore("another level change replaced it before it arrived");
    if (changes.Status() == LevelChangeStatus::Failed)
        return FailRestore(LevelChangeFailure(changes));
    if (changes.Status() == LevelChangeStatus::BodiesRestored && SectionsWaiting == nullptr)
        Restoring = RestoreProgress::BodiesRestored;
}

void SavedGames::FailWrite(std::string reason)
{
    Writing = SaveStatus::Failed;
    WriteReason = std::move(reason);
    Host.Logging().GetLogger<SavedGames>().Error("save: {}", WriteReason);
}

void SavedGames::FailRestore(std::string reason)
{
    Restoring = RestoreProgress::Failed;
    RestoreReason = std::move(reason);
    SectionsWaiting.reset();
    Host.Logging().GetLogger<SavedGames>().Error("restore: {}", RestoreReason);
}
