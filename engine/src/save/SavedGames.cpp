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
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{
    struct ReadOutcome
    {
        std::optional<SavedGame> Saved;
        std::string Error;
    };

    ReadOutcome ReadSlot(const SaveSlotFiles& files, const std::string& slot)
    {
        ReadOutcome outcome;
        std::string currentError = "no save in slot '" + slot + "'";
        if (const std::optional<std::vector<std::byte>> current = files.Read(slot))
            if ((outcome.Saved = DecodeSavedGame(*current, &currentError)))
                return outcome;
        std::string backupError;
        if (const std::optional<std::vector<std::byte>> backup = files.ReadBackup(slot))
            if ((outcome.Saved = DecodeSavedGame(*backup, &backupError)))
                return outcome;
        outcome.Error = currentError;
        return outcome;
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
    if (WriteTask.IsValid())
        (void)Host.Tasks().Cancel(WriteTask);
    if (ReadTask.IsValid())
        (void)Host.Tasks().Cancel(ReadTask);
}

bool SavedGames::RegisterSection(SavedGameSectionDefinition section)
{
    if (section.Name.empty() || std::ranges::any_of(Sections, [&](const SavedGameSectionDefinition& registered) {
            return registered.Name == section.Name;
        }))
        return false;
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
    if (!Host.Level().Loaded().has_value())
        return SaveAdmission::NothingLoaded;
    WriteWaiting = std::move(slot);
    Writing = SaveStatus::Pending;
    return SaveAdmission::Accepted;
}

SaveAdmission SavedGames::RequestRestore(std::string slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return SaveAdmission::BadSlot;
    if (RestoreWaiting.has_value() || Restoring == SaveStatus::Working)
        return SaveAdmission::Busy;
    if (!IsSimulationAuthority(Host.World().Entities()))
        return SaveAdmission::NotAuthority;
    if (const NetSession* session = Host.TryNet(); session != nullptr && session->Role() != NetSessionRole::Standalone)
        return SaveAdmission::InSession;
    RestoreWaiting = std::move(slot);
    Restoring = SaveStatus::Pending;
    return SaveAdmission::Accepted;
}

bool SavedGames::Holds(std::string_view slot) const
{
    if (Remembered.contains(slot))
        return true;
    std::error_code ignored;
    return Files.has_value() && SaveSlotFiles::IsSlotName(slot)
        && std::filesystem::exists(Files->PathOf(slot), ignored);
}

void SavedGames::Commit()
{
    if (WriteWaiting.has_value())
    {
        const std::string slot = std::move(*WriteWaiting);
        WriteWaiting.reset();
        Write(slot);
    }
    if (RestoreWaiting.has_value())
    {
        const std::string slot = std::move(*RestoreWaiting);
        RestoreWaiting.reset();
        Restore(slot);
    }
}

void SavedGames::Write(const std::string& slot)
{
    LoadedLevel& level = Host.Level();
    if (!level.Loaded().has_value())
        return FailWrite("nothing is loaded");
    level.CaptureState();

    World& world = Host.World().Entities();
    SavedGame saved;
    saved.Summary.Level = *level.Loaded();
    if (saved.Summary.Level.Kind == LevelKind::World && level.PlayZone().IsValid())
        saved.Summary.Focus = level.PlayZone();
    saved.Summary.SavedAtUnixSeconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    if (const ZoneStateStore* store = world.TryGetResource<ZoneStateStore>())
        saved.Zones = store->ExportDurable();
    for (const SavedGameSectionDefinition& section : Sections)
        if (section.Capture)
            saved.Sections.push_back(SavedGameSection{ section.Name, section.Version, section.Capture(world) });
    saved.Sections.insert(saved.Sections.end(), Unclaimed.begin(), Unclaimed.end());
    Remembered.insert_or_assign(slot, saved);

    if (!Files.has_value())
    {
        Writing = SaveStatus::Done;
        return;
    }
    Writing = SaveStatus::Working;
    WriteTask = Host.Tasks().Submit<bool>(
        [files = *Files, slot, saved] {
            const std::vector<std::byte> bytes = EncodeSavedGame(saved);
            return !bytes.empty() && files.Write(slot, bytes);
        },
        [this, slot](bool written) {
            WriteTask = {};
            if (written)
                Writing = SaveStatus::Done;
            else
                FailWrite("could not write '" + Files->PathOf(slot).string() + "'");
        });
}

void SavedGames::Restore(const std::string& slot)
{
    if (const auto remembered = Remembered.find(slot); remembered != Remembered.end())
        return BeginChange(remembered->second);
    if (!Files.has_value())
        return FailRestore("no save in slot '" + slot + "'");
    Restoring = SaveStatus::Working;
    ReadTask = Host.Tasks().Submit<ReadOutcome>([files = *Files, slot] { return ReadSlot(files, slot); },
                                                [this, slot](ReadOutcome outcome) {
                                                    ReadTask = {};
                                                    if (!outcome.Saved.has_value())
                                                        return FailRestore(std::move(outcome.Error));
                                                    Remembered.insert_or_assign(slot, *outcome.Saved);
                                                    BeginChange(*outcome.Saved);
                                                });
}

void SavedGames::BeginChange(const SavedGame& saved)
{
    const LevelChangeAdmission admission = Host.LevelChanges().Request(LevelChangeRequest{
        .Level = saved.Summary.Level, .Focus = saved.Summary.Focus, .ZoneState = saved.Zones });
    if (admission != LevelChangeAdmission::Accepted)
        return FailRestore(AdmissionReason(admission));
    RestoreSerial = Host.LevelChanges().Serial();
    SectionsWaiting = saved.Sections;
    Restoring = SaveStatus::Working;
}

void SavedGames::RestoreSections()
{
    if (!SectionsWaiting.has_value() || Host.LevelChanges().Serial() != RestoreSerial)
        return;
    const LevelChangeStatus change = Host.LevelChanges().Status();
    if (change == LevelChangeStatus::Pending)
        return;
    std::vector<SavedGameSection> saved = std::move(*SectionsWaiting);
    SectionsWaiting.reset();
    if (change == LevelChangeStatus::Failed)
        return FailRestore("the level change failed");

    World& world = Host.World().Entities();
    Unclaimed.clear();
    for (SavedGameSection& section : saved)
    {
        const auto registered = std::ranges::find(Sections, section.Name, &SavedGameSectionDefinition::Name);
        if (registered == Sections.end())
            Unclaimed.push_back(std::move(section));
        else if (registered->Restore && section.Version <= registered->Version)
            registered->Restore(world, section.Version, section.Data);
    }
    Restoring = SaveStatus::Done;
}

void SavedGames::FailWrite(std::string reason)
{
    Writing = SaveStatus::Failed;
    Reason = std::move(reason);
    Host.Logging().GetLogger<SavedGames>().Error("save: {}", Reason);
}

void SavedGames::FailRestore(std::string reason)
{
    Restoring = SaveStatus::Failed;
    Reason = std::move(reason);
    Host.Logging().GetLogger<SavedGames>().Error("restore: {}", Reason);
}
