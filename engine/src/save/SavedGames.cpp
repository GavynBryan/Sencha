#include <save/SavedGames.h>

#include <app/Engine.h>
#include <app/LevelChange.h>
#include <app/LoadedLevel.h>
#include <app/RuntimeContent.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <net/NetSession.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/RestoredBodies.h>
#include <save/HeldRecordSaves.h>
#include <world/RuntimeWorld.h>
#include <world/LevelLandmark.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentIdMint.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
    void AddHeldLists(const ZoneStateStore& store, std::vector<DurableLevelState>& levels,
                      std::vector<ScopedHeldRecords> held)
    {
        const auto before = [](const LevelReference& a, const LevelReference& b) {
            return a.Kind != b.Kind ? a.Kind < b.Kind : a.Name < b.Name;
        };
        for (ScopedHeldRecords& records : held)
        {
            const LevelReference* reference = store.LevelOf(records.Scope.Level);
            if (reference == nullptr)
                continue;
            auto level = std::ranges::find_if(levels, [&](const DurableLevelState& state) {
                return !before(state.Level, *reference);
            });
            if (level == levels.end() || level->Level != *reference)
                level = levels.insert(level, DurableLevelState{ *reference, {} });
            auto zone = std::ranges::find_if(level->Zones, [&](const DurableZoneState& state) {
                return state.Zone.Value >= records.Scope.Zone.Value;
            });
            if (zone == level->Zones.end() || zone->Zone != records.Scope.Zone)
                zone = level->Zones.insert(zone, DurableZoneState{ records.Scope.Zone, nullptr, nullptr });
            const ZoneStateList list{ std::string(kHeldZoneList), kZoneListVersion, EncodeEntityList(records.Records) };
            zone->Held = EncodeZoneLists(std::span(&list, 1));
        }
    }

    struct ReadOutcome
    {
        std::optional<SavedGame> Saved;
        std::string Error;
        bool FromBackup = false;
    };

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

    bool KeptAsBackup(std::span<const std::byte> bytes)
    {
        const DecodedSavedGame decoded = DecodeSavedGame(bytes);
        return decoded.Saved.has_value() || decoded.Newer;
    }

    bool WriteToDisk(const SaveSlotFiles& files, const std::string& slot, const SavedGame& saved)
    {
        const std::vector<std::byte> bytes = EncodeSavedGame(saved);
        if (bytes.empty())
            return false;
        if (const std::optional<std::vector<std::byte>> current = files.Read(slot); current && KeptAsBackup(*current))
            (void)files.WriteBackup(slot, *current);
        return files.Write(slot, bytes);
    }

    std::function<std::string()> WriteWork(const SaveSlotFiles& files, const std::string& slot,
                                           std::shared_ptr<const SavedGame> saved)
    {
        return [files, slot, saved] {
            return WriteToDisk(files, slot, *saved) ? std::string() : "could not write '" + files.PathOf(slot).string() + "'";
        };
    }

    bool CopySlot(const SaveSlotFiles& files, const std::string& from, const std::string& to, const SavedGame* unwritten)
    {
        std::optional<std::vector<std::byte>> current = files.Read(from);
        std::optional<std::vector<std::byte>> backup = files.ReadBackup(from);
        if (unwritten != nullptr)
        {
            if (current.has_value() && KeptAsBackup(*current))
                backup = std::move(current);
            current = EncodeSavedGame(*unwritten);
            if (current->empty())
                return false;
        }
        if (!current.has_value() && !backup.has_value())
            return false;
        const bool file = current.has_value() ? files.Write(to, *current) : files.Remove(to);
        return file && (backup.has_value() ? files.WriteBackup(to, *backup) : files.RemoveBackup(to));
    }

    void ListUnwritten(std::vector<SaveSlotSummary>& slots, const std::string& slot, const SavedGame& saved)
    {
        SaveSlotSummary unwritten{ .Slot = slot,
                                   .Summary = saved.Summary,
                                   .HasThumbnail = saved.Thumbnail.has_value(),
                                   .Newer = false,
                                   .FromBackup = false,
                                   .Error = {} };
        const auto at = std::ranges::lower_bound(slots, slot, {}, &SaveSlotSummary::Slot);
        if (at != slots.end() && at->Slot == slot)
            *at = std::move(unwritten);
        else
            slots.insert(at, std::move(unwritten));
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

    std::optional<ZoneId> ArrivalZone(Engine& engine, const std::string& arrival)
    {
        if (arrival.empty())
            return std::nullopt;
        const World& world = engine.World().Entities();
        const std::optional<LevelLandmarkPlace> landmark = FindLevelLandmark(world, arrival);
        if (!landmark.has_value())
        {
            engine.Logging().GetLogger<SavedGames>().Warn(
                "save: no loaded landmark is named '{}'; a restore looks for it once its level arrives", arrival);
            return std::nullopt;
        }
        const RuntimeZoneRecord* zone = engine.World().FindPartition(world.GetEntityPartition(landmark->Entity));
        return zone != nullptr ? std::optional<ZoneId>(zone->Id) : std::nullopt;
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

void SavedGames::KeepLevelState()
{
    Host.World().Entities().SetResource(
        ZoneStateSerialization{ .Serializers = &Host.SceneSerializers(), .Context = &Host.Content().SceneContext() });
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

SaveAdmission SavedGames::RequestWrite(std::string slot, std::string arrival)
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
    ArrivalWaiting = std::move(arrival);
    Writing = SaveStatus::Pending;
    return SaveAdmission::Accepted;
}

SaveAdmission SavedGames::RequestRestore(std::string slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return SaveAdmission::BadSlot;
    if (RestoreWaiting.has_value() || Restoring == RestoreProgress::Reading || Restoring == RestoreProgress::Loading
        || (InFlight.has_value() && !InFlight->From.empty() && InFlight->Slot == slot))
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

void SavedGames::SetSummary(SavedGameSummaryDefinition definition)
{
    SummaryDefinition = std::move(definition);
}

void SavedGames::RequestList(std::function<void(std::vector<SaveSlotSummary>)> done)
{
    ++ListingsOutstanding;
    (void)Host.Tasks().Submit<std::vector<SaveSlotSummary>>(
        [files = Files, slot = UnwrittenSlot, unwritten = Unwritten] {
            std::vector<SaveSlotSummary> slots = files.has_value() ? ListSaveSlots(*files).Slots
                                                                   : std::vector<SaveSlotSummary>{};
            if (unwritten != nullptr)
                ListUnwritten(slots, *slot, *unwritten);
            return slots;
        },
        [this, deletedBefore = DeletedSinceListing.size(), done = std::move(done)](std::vector<SaveSlotSummary> slots) {
            std::erase_if(slots, [&](const SaveSlotSummary& listed) { return DeletedSince(deletedBefore, listed.Slot); });
            Answered();
            done(std::move(slots));
        });
}

void SavedGames::RequestThumbnail(std::string slot, std::function<void(std::optional<SavedGameThumbnail>)> done)
{
    const std::shared_ptr<const SavedGame> unwritten = UnwrittenSlot == slot ? Unwritten : nullptr;
    ++ListingsOutstanding;
    (void)Host.Tasks().Submit<std::optional<SavedGameThumbnail>>(
        [files = Files, slot, unwritten]() -> std::optional<SavedGameThumbnail> {
            if (unwritten != nullptr)
                return unwritten->Thumbnail.has_value() ? DecodeSavedGameThumbnail(*unwritten->Thumbnail) : std::nullopt;
            if (!files.has_value() || !SaveSlotFiles::IsSlotName(slot))
                return std::nullopt;
            return ReadSaveSlotThumbnail(*files, slot);
        },
        [this, slot, deletedBefore = DeletedSinceListing.size(), done = std::move(done)](
            std::optional<SavedGameThumbnail> thumbnail) {
            if (DeletedSince(deletedBefore, slot))
                thumbnail.reset();
            Answered();
            done(std::move(thumbnail));
        });
}

bool SavedGames::DeletedSince(std::size_t deletedBefore, std::string_view slot) const
{
    return std::ranges::find(DeletedSinceListing.begin() + static_cast<std::ptrdiff_t>(deletedBefore),
                             DeletedSinceListing.end(), slot)
        != DeletedSinceListing.end();
}

void SavedGames::Answered()
{
    if (--ListingsOutstanding == 0)
        DeletedSinceListing.clear();
}

SaveAdmission SavedGames::RequestDelete(std::string slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return SaveAdmission::BadSlot;
    if (WritesTo(slot) || RestoresFrom(slot))
        return SaveAdmission::Busy;
    if (ListingsOutstanding > 0)
        DeletedSinceListing.push_back(slot);
    Touched.erase(slot);
    if (UnwrittenSlot == slot)
    {
        Unwritten.reset();
        UnwrittenSlot.reset();
    }
    if (Files.has_value() && !Files->Remove(slot))
        Host.Logging().GetLogger<SavedGames>().Error("save: could not delete '{}'", Files->PathOf(slot).string());
    return SaveAdmission::Accepted;
}

SaveAdmission SavedGames::RequestCopy(std::string from, std::string to)
{
    if (!SaveSlotFiles::IsSlotName(from) || !SaveSlotFiles::IsSlotName(to))
        return SaveAdmission::BadSlot;
    if (WriteWaiting.has_value() || Writing == SaveStatus::Working)
        return SaveAdmission::Busy;
    if (from == to)
        return SaveAdmission::Accepted;
    if (!Files.has_value())
    {
        FailWrite("saves are kept in memory only, so there is no file to copy");
        return SaveAdmission::Accepted;
    }
    std::shared_ptr<const SavedGame> source = UnwrittenSlot == from ? Unwritten : nullptr;
    const std::string failure = "could not copy slot '" + from + "' to '" + to + "'";
    StartWrite(InFlightWrite{ .Slot = to,
                              .From = from,
                              .Saved = nullptr,
                              .Work = [files = *Files, from, to, source, failure] {
                                  return CopySlot(files, from, to, source.get()) ? std::string() : failure;
                              },
                              .FromRemembered = Touched.contains(from),
                              .Task = {} });
    return SaveAdmission::Accepted;
}

bool SavedGames::WritesTo(std::string_view slot) const
{
    return WriteWaiting == slot || (InFlight.has_value() && (InFlight->Slot == slot || InFlight->From == slot));
}

bool SavedGames::RestoresFrom(std::string_view slot) const
{
    return RestoreWaiting == slot
        || ((Restoring == RestoreProgress::Reading || Restoring == RestoreProgress::Loading) && RestoringSlot == slot);
}

bool SavedGames::Holds(std::string_view slot) const
{
    if (Unwritten != nullptr && UnwrittenSlot == slot)
        return true;
    return Files.has_value() && SaveSlotFiles::IsSlotName(slot) && ReadSaveSlotSummary(*Files, slot).Readable();
}

void SavedGames::Commit()
{
    if (WriteWaiting.has_value())
    {
        if (LevelSettling())
            return CommitRestore();
        const std::optional<std::string> refused = WriteRefusal();
        const std::string slot = std::move(*WriteWaiting);
        const std::string arrival = std::exchange(ArrivalWaiting, std::string());
        WriteWaiting.reset();
        if (refused.has_value())
            FailWrite(*refused);
        else if (const std::shared_ptr<const SavedGame> saved = Capture(slot, arrival))
            StartWrite(InFlightWrite{ .Slot = slot, .From = {}, .Saved = saved, .Work = WriteWork(*Files, slot, saved),
                                      .FromRemembered = false, .Task = {} });
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

bool SavedGames::LevelSettling() const
{
    return LevelChanging(Host.LevelChanges().Status()) || Host.Level().Residency() == LevelResidency::Loading;
}

std::optional<std::string> SavedGames::WriteRefusal() const
{
    if (Host.LevelChanges().Status() == LevelChangeStatus::Failed)
        return std::string("the level change it waited for failed");
    switch (Host.Level().Residency())
    {
    case LevelResidency::Resident:
        break;
    case LevelResidency::Failed:
        return std::string("the level failed to load");
    case LevelResidency::Unloaded:
    case LevelResidency::Loading:
        return std::string("nothing is loaded");
    }
    const World& world = Host.World().Entities();
    if (const EntityId participant = LocalParticipantOf(world);
        participant.IsValid() && !LivingBodyOf(world, participant).IsValid())
        return std::string("the local participant has no body");
    return std::nullopt;
}

std::shared_ptr<const SavedGame> SavedGames::Capture(const std::string& slot, const std::string& arrival)
{
    World& world = Host.World().Entities();
    SettleHeldRecords(world);
    LoadedLevel& level = Host.Level();
    level.CaptureState();
    CapturingArrival = arrival;

    auto saved = std::make_shared<SavedGame>();
    saved->Summary.Level = *level.Loaded();
    const std::optional<ZoneId> arrivalZone = ArrivalZone(Host, arrival);
    if (saved->Summary.Level.Kind == LevelKind::World && level.PlayZone().IsValid())
        saved->Summary.Focus = arrivalZone.value_or(level.PlayZone());
    saved->Summary.SavedAtUnixSeconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    if (SummaryDefinition.Describe)
        saved->Summary.Game = SummaryDefinition.Describe(world);
    if (SummaryDefinition.Thumbnail)
        if (const std::optional<SavedGameThumbnail> thumbnail = SummaryDefinition.Thumbnail(world))
            saved->Thumbnail = EncodeSavedGameThumbnail(*thumbnail);
    if (const ZoneStateStore* store = world.TryGetResource<ZoneStateStore>())
    {
        saved->Levels = store->ExportDurable();
        const RuntimeWorld& runtime = Host.World();
        const ZoneStateScope worldScene = level.WorldSceneStateScope();
        const auto scopeOf = [&](EntityId holder) {
            const StoragePartitionId partition = world.GetEntityPartition(holder);
            if (partition == PersistentStoragePartition)
                return worldScene;
            const RuntimeZoneRecord* record = runtime.FindPartition(partition);
            return record != nullptr ? record->StateScope : ZoneStateScope{};
        };
        AddHeldLists(*store, saved->Levels,
                     CaptureLevelHeldRecords(world, scopeOf, Host.SceneSerializers(), Host.Content().SceneContext()));
    }
    for (const SavedGameSectionDefinition& section : Sections)
        if (section.Capture)
            saved->Sections.push_back(SavedGameSection{ section.Name, section.Version, section.Capture(world) });
    CapturingArrival.clear();
    saved->Sections.insert(saved->Sections.end(), Unclaimed.begin(), Unclaimed.end());
    saved->Sections.push_back(CapturePersistentIds(world));
    saved->Carried = UnclaimedChunks;
    Touched.insert(slot);
    UnwrittenSlot = slot;
    Unwritten = saved;
    if (!Files.has_value())
    {
        Writing = SaveStatus::Done;
        return nullptr;
    }
    return saved;
}

void SavedGames::StartWrite(InFlightWrite write)
{
    Writing = SaveStatus::Working;
    InFlight = std::move(write);
    InFlight->Task = Host.Tasks().Submit<std::string>(InFlight->Work,
                                                      [this](std::string failure) { FinishWrite(std::move(failure)); });
}

void SavedGames::FinishWrite(std::string failure)
{
    const InFlightWrite done = std::move(*InFlight);
    InFlight.reset();
    if (!failure.empty())
        return FailWrite(std::move(failure));
    if (done.From.empty() ? Unwritten == done.Saved : UnwrittenSlot == done.Slot)
    {
        Unwritten.reset();
        UnwrittenSlot.reset();
    }
    if (!done.From.empty() && done.FromRemembered)
        Touched.insert(done.Slot);
    else if (!done.From.empty())
        Touched.erase(done.Slot);
    Writing = SaveStatus::Done;
}

void SavedGames::Finish()
{
    if (WriteWaiting.has_value())
    {
        const std::optional<std::string> refused =
            LevelSettling() ? std::optional<std::string>("the game quit while the level was changing") : WriteRefusal();
        const std::string slot = std::move(*WriteWaiting);
        const std::string arrival = std::exchange(ArrivalWaiting, std::string());
        WriteWaiting.reset();
        if (refused.has_value())
            return FailWrite(*refused);
        if (const std::shared_ptr<const SavedGame> saved = Capture(slot, arrival))
        {
            InFlight = InFlightWrite{ .Slot = slot, .From = {}, .Saved = saved, .Work = WriteWork(*Files, slot, saved),
                                      .FromRemembered = false, .Task = {} };
            FinishWrite(InFlight->Work());
        }
        return;
    }
    if (InFlight.has_value() && Host.Tasks().GetState(InFlight->Task) == AsyncTaskState::Pending
        && Host.Tasks().Cancel(InFlight->Task))
        FinishWrite(InFlight->Work());
}

void SavedGames::Restore(const std::string& slot)
{
    RestoringSlot = slot;
    if (Unwritten != nullptr && UnwrittenSlot == slot)
        return BeginChange(Unwritten);
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
                                                    Touched.insert(slot);
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
    LevelChangeRequest request = LevelChangeRequest::Restore(saved->Summary.Level, saved->Levels);
    if (saved->Summary.Focus.has_value())
        request.Focused(*saved->Summary.Focus);
    const LevelChangeAdmission admission = Host.LevelChanges().Request(std::move(request));
    if (admission != LevelChangeAdmission::Accepted)
        return FailRestore(LevelChangeAdmissionReason(admission));
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
        if (SeenChange != RestoreSerial && changes.Kind() == LevelChangeKind::NewGame)
        {
            Unclaimed.clear();
            UnclaimedChunks.clear();
        }
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
    UnclaimedChunks = saved->Carried;
    RestoreHeldLists(world, *saved);
}

void SavedGames::RestoreHeldLists(World& world, const SavedGame& saved)
{
    ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    if (store == nullptr)
        return;
    std::vector<ZoneStateLevelId> away;
    for (const DurableLevelState& level : saved.Levels)
        for (const DurableZoneState& zone : level.Zones)
        {
            const std::optional<std::vector<ZoneStateList>> lists =
                zone.Held != nullptr ? DecodeZoneLists(*zone.Held) : std::nullopt;
            if (!lists.has_value())
                continue;
            const ZoneStateScope scope = store->Scope(level.Level, zone.Zone);
            for (const ZoneStateList& list : *lists)
                if (const std::optional<std::vector<ZoneListEntity>> records = DecodeEntityList(list.Payload))
                    RestoreLevelHeldRecords(world, scope, *records, Host.SceneSerializers(),
                                            Host.Content().SceneContext());
            if (level.Level != saved.Summary.Level)
                away.push_back(scope.Level);
        }
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        for (const ZoneStateLevelId level : away)
            index->SetAside(level);
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
    if (RestoredBodies* restored = Host.World().Entities().TryGetResource<RestoredBodies>();
        restored != nullptr && !restored->Refusal.empty())
        return FailRestore(std::exchange(restored->Refusal, std::string()));
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
