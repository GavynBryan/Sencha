#include "WorldDocument.h"

#include "ZoneBounds.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/ZoneDemand.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{

ZoneId UniqueZoneAt(const WorldPartitionManifest& manifest, Vec3d point)
{
    ZoneId result;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (!zone.Bounds.Contains(point))
            continue;
        if (result.IsValid())
            return {};
        result = zone.Id;
    }
    return result;
}

// Scene file names derive from zone names: lowercase, spaces to underscores,
// ASCII alphanumerics/underscore/hyphen only. Empty after sanitizing falls
// back to "zone".
std::string SanitizeZoneFileName(std::string_view name)
{
    std::string result;
    result.reserve(name.size());
    for (const char c : name)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc > 0x7f)
            continue;
        if (c == ' ')
            result.push_back('_');
        else if (std::isalnum(uc) || c == '_' || c == '-')
            result.push_back(static_cast<char>(std::tolower(uc)));
    }
    if (result.empty())
        result = "zone";
    return result;
}

} // namespace

WorldDocument::WorldDocument(LoggingProvider& logging)
    : Logging_(logging)
    , LegacyDocument_(std::make_unique<EditorDocument>(logging))
    , Rng_(std::random_device{}())
{
}

WorldDocument::~WorldDocument()
{
    WriteUserSidecar();
}

void WorldDocument::SetAssetEnvironment(RuntimeAssets& assets)
{
    Assets_ = &assets;
    if (LegacyDocument_)
        LegacyDocument_->SetAssetEnvironment(assets);
    if (WorldScene_)
        WorldScene_->SetAssetEnvironment(assets);
    for (auto& [zone, open] : OpenZones_)
        open.Document->SetAssetEnvironment(assets);
}

bool WorldDocument::LoadWorld(std::string_view path)
{
    auto& log = Logging_.GetLogger<WorldDocument>();

    std::ifstream file{ std::string(path), std::ios::binary };
    if (!file.is_open())
    {
        log.Error("cannot open world file '{}'", path);
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
    {
        log.Error("world file '{}' is not valid JSON: {}", path, parseError.Message);
        return false;
    }

    std::string manifestError;
    std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*root, &manifestError);
    if (!manifest.has_value())
    {
        log.Error("world file '{}' rejected: {}", path, manifestError);
        return false;
    }

    // The file is accepted from here on, so the open documents are going away:
    // anything holding them must unwind before they do.
    NotifyWillReplaceDocuments();

    WriteUserSidecar();
    WorldMode_ = true;
    LegacyDocument_.reset();
    OpenZones_.clear();
    FocusZone_ = ZoneId{};
    SelectedZone_ = ZoneId{};
    WorldSceneFocused_ = false;
    WorldPath_.assign(path);
    Manifest_ = std::move(*manifest);
    IndexDirty_ = true;
    WorldDirty_ = false;
    CreateWorldSceneDocument();

    const SidecarFocus sidecar = ApplyUserSidecar();
    ZoneId focus = sidecar.Zone;
    if (!focus.IsValid() || FindZoneHeader(focus) == nullptr)
        focus = Manifest_.StartZone;
    if (!focus.IsValid() || FindZoneHeader(focus) == nullptr)
        focus = Manifest_.Zones.empty() ? ZoneId{} : Manifest_.Zones.front().Id;

    if (!focus.IsValid() || !SetFocusZone(focus))
    {
        // A world the editor cannot focus into is unusable: fall back to a fresh
        // legacy document so the workspace always has an editable document.
        log.Error("world file '{}' has no loadable zone to focus", path);
        WorldMode_ = false;
        WorldPath_.clear();
        Manifest_ = {};
        IndexDirty_ = true;
        OpenZones_.clear();
        WorldScene_.reset();
        WorldSceneFocused_ = false;
        LegacyDocument_ = std::make_unique<EditorDocument>(Logging_);
        if (Assets_)
            LegacyDocument_->SetAssetEnvironment(*Assets_);
        NotifyEditedDocumentChanged();
        return false;
    }
    if (sidecar.WorldScene)
        FocusWorldScene();
    RunValidation();
    return true;
}

bool WorldDocument::SaveWorld()
{
    assert(WorldMode_ && "SaveWorld: world mode only");
    auto& log = Logging_.GetLogger<WorldDocument>();
    if (WorldPath_.empty())
        return false;
    if (!Manifest_.Transitions.empty())
    {
        log.Error("cannot save world with {} unresolved legacy transitions; migrate or discard them",
                  Manifest_.Transitions.size());
        return false;
    }

    namespace fs = std::filesystem;

    // The world scene ref derives from the world file's stem; assigned before
    // the zone refs so new zones dodge it. A rare stem collision with an
    // existing zone scene gets the same numeric-suffix escape new zones use.
    if (Manifest_.WorldSceneRef.empty())
    {
        const std::string base = SanitizeZoneFileName(fs::path(WorldPath_).stem().string());
        std::string candidate = "levels/" + base + "_world.sscene";
        for (int suffix = 2;; ++suffix)
        {
            const auto taken = std::any_of(Manifest_.Zones.begin(), Manifest_.Zones.end(),
                                           [&](const ZoneHeader& zone)
                                           { return zone.SceneRef == candidate; });
            if (!taken)
                break;
            candidate = "levels/" + base + "_world_" + std::to_string(suffix) + ".sscene";
        }
        Manifest_.WorldSceneRef = std::move(candidate);
        MarkManifestEdited();
    }

    AssignSceneRefsForNewZones();
    // Refresh derived bounds before the zone files write so they persist current.
    RefreshDerivedZoneBounds();
    for (ZoneHeader& header : Manifest_.Zones)
    {
        const std::string scenePath = ResolveScenePath(header.SceneRef);
        std::error_code ec;
        fs::create_directories(fs::path(scenePath).parent_path(), ec);

        const auto it = OpenZones_.find(header.Id);
        if (it != OpenZones_.end())
        {
            EditorDocument& document = *it->second.Document;
            if (!document.HasFilePath())
            {
                if (!document.SaveAs(scenePath))
                {
                    log.Error("failed to save zone scene '{}'", scenePath);
                    return false;
                }
            }
            else if (document.IsDirty() && !document.Save())
            {
                log.Error("failed to save zone scene '{}'", scenePath);
                return false;
            }
        }
        else if (!fs::exists(scenePath, ec))
        {
            // Header-only zone that has never had a scene: write an empty one so
            // the manifest never references a missing file.
            EditorDocument empty(Logging_);
            if (!empty.SaveAs(scenePath))
            {
                log.Error("failed to write scene '{}' for zone without content", scenePath);
                return false;
            }
        }
    }

    // The world scene writes beside the zone scenes, same discipline: a fresh
    // document lands at its ref so the manifest never references a missing file.
    {
        const std::string scenePath = ResolveScenePath(Manifest_.WorldSceneRef);
        std::error_code ec;
        fs::create_directories(fs::path(scenePath).parent_path(), ec);
        if (!WorldScene_->HasFilePath())
        {
            if (!WorldScene_->SaveAs(scenePath))
            {
                log.Error("failed to save world scene '{}'", scenePath);
                return false;
            }
        }
        else if (WorldScene_->IsDirty() && !WorldScene_->Save())
        {
            log.Error("failed to save world scene '{}'", scenePath);
            return false;
        }
    }

    const std::string text = JsonStringify(WriteWorldPartitionManifest(Manifest_), /*pretty*/ true);
    std::ofstream file(WorldPath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        log.Error("cannot write world file '{}'", WorldPath_);
        return false;
    }
    file << text;
    if (!file.good())
        return false;

    WorldDirty_ = false;
    WriteUserSidecar();
    RunValidation();
    return true;
}

bool WorldDocument::SaveWorldAs(std::string_view path)
{
    if (path.empty())
        return false;
    // Whatever the save dialog produced, the world lands with the extension
    // the open routing fast-paths on.
    std::filesystem::path normalized{ std::string(path) };
    if (normalized.extension() != ".sworld")
        normalized.replace_extension(".sworld");
    WorldPath_ = normalized.string();
    return SaveWorld();
}

void WorldDocument::NewWorld(std::string_view name)
{
    NotifyWillReplaceDocuments();

    WriteUserSidecar();
    WorldMode_ = true;
    LegacyDocument_.reset();
    OpenZones_.clear();
    FocusZone_ = ZoneId{};
    SelectedZone_ = ZoneId{};
    WorldSceneFocused_ = false;
    WorldPath_.clear();
    Manifest_ = {};
    Manifest_.Name.assign(name);
    IndexDirty_ = true;
    CreateWorldSceneDocument();

    const GraphId graph = AddGraph("Graph 1");
    const ZoneId zone = AddZone(graph, "Zone 1");
    Manifest_.StartZone = zone;
    WorldDirty_ = true;
    SetFocusZone(zone);
}

bool WorldDocument::HasSaveTarget() const
{
    if (WorldMode_)
        return !WorldPath_.empty();
    return LegacyDocument_->HasFilePath();
}

void WorldDocument::New()
{
    // Both branches discard document content: the world teardown drops every
    // open zone, and the legacy path clears the scene in place.
    NotifyWillReplaceDocuments();

    if (WorldMode_)
        CloseWorldToLegacy();
    LegacyDocument_->New();
    NotifyEditedDocumentChanged();
}

bool WorldDocument::IsWorldManifestFile(std::string_view path)
{
    std::ifstream file(std::string(path), std::ios::binary);
    if (!file.is_open())
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto json = JsonParse(buffer.str());
    if (!json.has_value() || !json->IsObject())
        return false;
    return json->Find("format_version") != nullptr && json->Find("zones") != nullptr;
}

bool WorldDocument::Load(std::string_view path)
{
    // A world manifest saved under any extension still opens as a world;
    // routing by content keeps a mistyped Save As name from producing a file
    // that only ever opens blank.
    if (IsWorldManifestFile(path))
        return LoadWorld(path);

    // Staged: a file that parses but does not load must not cost the user what
    // is already open. Only a document that came up whole replaces the current
    // one, so a rejected file leaves the session exactly as it was.
    auto staged = std::make_unique<EditorDocument>(Logging_);
    if (Assets_)
        staged->SetAssetEnvironment(*Assets_);
    if (!staged->Load(path))
        return false;

    NotifyWillReplaceDocuments();
    if (WorldMode_)
        CloseWorldState();
    LegacyDocument_ = std::move(staged);
    NotifyEditedDocumentChanged();
    return true;
}

bool WorldDocument::Save()
{
    if (WorldMode_)
        return SaveWorld();
    return LegacyDocument_->Save();
}

bool WorldDocument::SaveAs(std::string_view path)
{
    if (WorldMode_)
        return SaveWorldAs(path);
    return LegacyDocument_->SaveAs(path);
}

WorldPartitionManifest& WorldDocument::Manifest()
{
    assert(WorldMode_ && "Manifest: world mode only");
    return Manifest_;
}

const WorldPartitionIndex& WorldDocument::Index() const
{
    assert(WorldMode_ && "Index: world mode only");
    if (IndexDirty_)
    {
        Index_ = WorldPartitionIndex::Build(Manifest_);
        IndexDirty_ = false;
    }
    return Index_;
}

bool WorldDocument::LoadZone(ZoneId zone)
{
    auto& log = Logging_.GetLogger<WorldDocument>();
    if (!WorldMode_)
        return false;
    if (IsZoneOpen(zone))
        return true;

    const ZoneHeader* header = FindZoneHeader(zone);
    if (header == nullptr)
    {
        log.Error("LoadZone: zone {} is not in the manifest", ZoneIdToString(zone));
        return false;
    }

    auto document = std::make_unique<EditorDocument>(Logging_);
    document->OnEdited = [this] { Revalidate(); };
    document->SetRegistryIdentity(RegistryId{ NextRegistryIndex_++, 1 }, zone);
    if (Assets_)
        document->SetAssetEnvironment(*Assets_);

    if (!header->SceneRef.empty())
    {
        const std::string scenePath = ResolveScenePath(header->SceneRef);
        if (!document->Load(scenePath))
        {
            log.Error("LoadZone: cannot load scene '{}' for zone {}", scenePath,
                      ZoneIdToString(zone));
            return false;
        }
    }

    OpenZones_.emplace(zone, OpenZone{ std::move(document), ZoneViewState{} });
    return true;
}

bool WorldDocument::UnloadZone(ZoneId zone)
{
    auto& log = Logging_.GetLogger<WorldDocument>();
    const auto it = OpenZones_.find(zone);
    if (it == OpenZones_.end())
        return false;
    if (zone == FocusZone_)
    {
        log.Warn("UnloadZone: zone {} is the focus zone; focus another zone first",
                 ZoneIdToString(zone));
        return false;
    }
    if (it->second.Document->IsDirty())
    {
        log.Warn("UnloadZone: zone {} has unsaved changes; save first", ZoneIdToString(zone));
        return false;
    }
    OpenZones_.erase(it);
    if (OnZoneUnloaded)
        OnZoneUnloaded(zone);
    return true;
}

bool WorldDocument::IsZoneOpen(ZoneId zone) const
{
    return OpenZones_.contains(zone);
}

EditorDocument* WorldDocument::ZoneDocument(ZoneId zone)
{
    if (!WorldMode_)
        return nullptr;
    const auto it = OpenZones_.find(zone);
    return it != OpenZones_.end() ? it->second.Document.get() : nullptr;
}

bool WorldDocument::SetZoneVisible(ZoneId zone, bool visible)
{
    const auto it = OpenZones_.find(zone);
    if (it == OpenZones_.end())
        return false;
    it->second.View.VisibleInEditor = visible;
    return true;
}

bool WorldDocument::SetFocusZone(ZoneId zone)
{
    if (!WorldMode_)
        return false;
    if (zone == FocusZone_ && IsZoneOpen(zone))
    {
        SelectedZone_ = zone;
        return true;
    }
    if (!LoadZone(zone))
        return false;

    FocusZone_ = zone;
    SelectedZone_ = zone;
    WorldSceneFocused_ = false;
    NotifyEditedDocumentChanged();
    return true;
}

bool WorldDocument::SelectZone(ZoneId zone)
{
    if (!WorldMode_)
        return false;
    if (zone.IsValid() && FindZoneHeader(zone) == nullptr)
        return false;
    SelectedZone_ = zone;
    return true;
}

bool WorldDocument::FocusWorldScene()
{
    if (!WorldMode_)
        return false;
    if (WorldSceneFocused_)
        return true;

    // Every zone becomes context: no zone focus survives, so any of them may
    // unload while the world scene is edited.
    WorldSceneFocused_ = true;
    FocusZone_ = ZoneId{};
    SelectedZone_ = ZoneId{};
    NotifyEditedDocumentChanged();
    return true;
}

EditorDocument& WorldDocument::WorldSceneDocument()
{
    assert(WorldMode_ && "WorldSceneDocument: world mode only");
    return *WorldScene_;
}

const EditorDocument& WorldDocument::WorldSceneDocument() const
{
    assert(WorldMode_ && "WorldSceneDocument: world mode only");
    return *WorldScene_;
}

EditorDocument& WorldDocument::FocusDocument()
{
    if (!WorldMode_)
        return *LegacyDocument_;
    if (WorldSceneFocused_)
        return *WorldScene_;
    const auto it = OpenZones_.find(FocusZone_);
    assert(it != OpenZones_.end() && "FocusDocument: focus zone is not open");
    return *it->second.Document;
}

const EditorDocument& WorldDocument::FocusDocument() const
{
    if (!WorldMode_)
        return *LegacyDocument_;
    if (WorldSceneFocused_)
        return *WorldScene_;
    const auto it = OpenZones_.find(FocusZone_);
    assert(it != OpenZones_.end() && "FocusDocument: focus zone is not open");
    return *it->second.Document;
}

bool WorldDocument::IsDirty() const
{
    if (!WorldMode_)
        return LegacyDocument_->IsDirty();
    if (WorldDirty_)
        return true;
    if (WorldScene_ && WorldScene_->IsDirty())
        return true;
    for (const auto& [zone, open] : OpenZones_)
    {
        if (open.Document->IsDirty())
            return true;
    }
    return false;
}

ZoneId WorldDocument::MintZoneId()
{
    return ZoneId{ MintRawId() };
}

GraphId WorldDocument::MintGraphId()
{
    return GraphId{ MintRawId() };
}

DockId WorldDocument::MintDockId()
{
    return DockId{ MintRawId() };
}

LinkId WorldDocument::MintLinkId()
{
    return LinkId{ MintRawId() };
}

ZoneId WorldDocument::AddZone(GraphId graph, std::string name)
{
    assert(WorldMode_ && "AddZone: world mode only");
    ZoneHeader header;
    header.Id = MintZoneId();
    header.Name = std::move(name);
    header.Graph = graph;
    header.Bounds = Aabb3d::FromMinMax(
        Vec3d{ -5.0f, 0.0f, -5.0f }, Vec3d{ 5.0f, 4.0f, 5.0f });
    Manifest_.Zones.push_back(std::move(header));
    MarkManifestEdited();
    RunValidation();
    return Manifest_.Zones.back().Id;
}

GraphId WorldDocument::AddGraph(std::string name)
{
    assert(WorldMode_ && "AddGraph: world mode only");
    GraphRecord record;
    record.Id = MintGraphId();
    record.Name = std::move(name);
    Manifest_.Graphs.push_back(std::move(record));
    MarkManifestEdited();
    RunValidation();
    return Manifest_.Graphs.back().Id;
}

LegacyTransitionMigrationReport WorldDocument::MigrateLegacyTransitions()
{
    assert(WorldMode_ && "MigrateLegacyTransitions: world mode only");
    LegacyTransitionMigrationReport report;
    if (Manifest_.Transitions.empty())
        return report;

    std::vector<const TransitionRecord*> ordered;
    ordered.reserve(Manifest_.Transitions.size());
    for (const TransitionRecord& transition : Manifest_.Transitions)
        ordered.push_back(&transition);
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b)
              { return a->Id.Value < b->Id.Value; });

    std::vector<TransitionId> migrated;
    const auto consumed = [&](TransitionId id)
    {
        return std::find(migrated.begin(), migrated.end(), id) != migrated.end();
    };
    const auto matchingReverse = [](const TransitionRecord& a,
                                    const TransitionRecord& b)
    {
        return b.Topology == TransitionTopology::Teleport
            && a.From == b.To && a.To == b.From
            && !a.Flags.OneWay && !b.Flags.OneWay;
    };

    EditorScene& scene = WorldScene_->GetScene();
    Registry& registry = scene.GetRegistry();
    for (const TransitionRecord* transition : ordered)
    {
        if (consumed(transition->Id))
            continue;
        if (transition->Topology != TransitionTopology::Teleport)
        {
            report.Unresolved.push_back(transition->Id);
            continue;
        }

        const TransitionRecord* reverse = nullptr;
        for (const TransitionRecord* candidate : ordered)
        {
            if (candidate == transition || consumed(candidate->Id))
                continue;
            if (matchingReverse(*transition, *candidate))
            {
                reverse = candidate;
                break;
            }
        }

        WorldLink link;
        link.Id = MintLinkId();
        link.ZoneA = transition->From;
        link.ZoneB = transition->To;
        link.Kind = LinkKind::Teleport;
        link.Directions = reverse != nullptr ? DockDirectionBoth : DockDirectionAToB;

        const EntityId entity = registry.Components.CreateEntity();
        registry.Components.AddComponent(entity, link);
        scene.TrackEntity(entity);
        migrated.push_back(transition->Id);
        if (reverse != nullptr)
        {
            migrated.push_back(reverse->Id);
            ++report.CollapsedPairs;
        }
        report.Links.push_back({ transition->Id,
                                 reverse != nullptr ? reverse->Id : TransitionId{},
                                 link.Id });
    }

    if (!migrated.empty())
    {
        std::erase_if(Manifest_.Transitions, [&](const TransitionRecord& transition)
                      { return consumed(transition.Id); });
        WorldScene_->MarkDirty();
        MarkManifestEdited();
    }
    RunValidation();
    return report;
}

bool WorldDocument::ConvertLegacyTransitionToTeleport(TransitionId id)
{
    assert(WorldMode_ && "ConvertLegacyTransitionToTeleport: world mode only");
    const auto it = std::find_if(
        Manifest_.Transitions.begin(), Manifest_.Transitions.end(),
        [&](const TransitionRecord& record) { return record.Id == id; });
    if (it == Manifest_.Transitions.end())
        return false;

    const TransitionRecord source = *it;
    TransitionId reverseId;
    if (!source.Flags.OneWay)
        for (const TransitionRecord& candidate : Manifest_.Transitions)
            if (candidate.Id != source.Id && !candidate.Flags.OneWay
                && candidate.From == source.To && candidate.To == source.From)
            {
                reverseId = candidate.Id;
                break;
            }

    WorldLink link;
    link.Id = MintLinkId();
    link.ZoneA = source.From;
    link.ZoneB = source.To;
    link.Kind = LinkKind::Teleport;
    link.Directions = reverseId.IsValid()
        ? DockDirectionBoth : DockDirectionAToB;

    EditorScene& scene = WorldScene_->GetScene();
    Registry& registry = scene.GetRegistry();
    const EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, link);
    scene.TrackEntity(entity);

    std::erase_if(Manifest_.Transitions, [&](const TransitionRecord& record)
                  { return record.Id == source.Id || record.Id == reverseId; });
    WorldScene_->MarkDirty();
    MarkManifestEdited();
    RunValidation();
    return true;
}

bool WorldDocument::DiscardLegacyTransition(TransitionId transition)
{
    const auto count = std::erase_if(Manifest_.Transitions,
                                     [&](const TransitionRecord& record)
                                     { return record.Id == transition; });
    if (count == 0)
        return false;
    MarkManifestEdited();
    RunValidation();
    return true;
}

void WorldDocument::Revalidate()
{
    RefreshDerivedZoneBounds();
    RunValidation();
}

void WorldDocument::RefreshDerivedZoneBounds()
{
    if (!WorldMode_)
        return;

    // Derived zones follow currently open, boundable content. Closed or empty
    // zones retain their persisted starter/cache value.
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.BoundsOverridden)
            continue;
        const auto it = OpenZones_.find(header.Id);
        if (it == OpenZones_.end())
            continue;
        if (const auto bounds = ComputeZoneBounds(it->second.Document->GetScene()))
            header.Bounds = *bounds;
    }
}

bool WorldDocument::RenameZone(ZoneId zone, std::string name)
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id != zone)
            continue;
        header.Name = std::move(name);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetZoneGraph(ZoneId zone, GraphId graph)
{
    const bool graphExists = std::any_of(Manifest_.Graphs.begin(), Manifest_.Graphs.end(),
                                         [&](const GraphRecord& record)
                                         { return record.Id == graph; });
    if (!graphExists)
        return false;
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id != zone)
            continue;
        header.Graph = graph;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetZoneBounds(ZoneId zone, Aabb3d bounds, bool overridden)
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id != zone)
            continue;
        header.Bounds = bounds;
        header.BoundsOverridden = overridden;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::UseDerivedZoneBounds(ZoneId zone)
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id != zone)
            continue;
        header.BoundsOverridden = false;
        const auto it = OpenZones_.find(header.Id);
        if (it != OpenZones_.end())
            if (const auto bounds = ComputeZoneBounds(it->second.Document->GetScene()))
                header.Bounds = *bounds;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::RenameGraph(GraphId graph, std::string name)
{
    for (GraphRecord& record : Manifest_.Graphs)
    {
        if (record.Id != graph)
            continue;
        record.Name = std::move(name);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetGraphHopCount(GraphId graph, std::optional<int32_t> hopCount)
{
    for (GraphRecord& record : Manifest_.Graphs)
    {
        if (record.Id != graph)
            continue;
        record.Streaming.HopCount = hopCount;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetGraphRadius(GraphId graph, std::optional<double> radius)
{
    for (GraphRecord& record : Manifest_.Graphs)
    {
        if (record.Id != graph)
            continue;
        record.Streaming.Radius = radius;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetGraphResidentCap(GraphId graph, std::optional<int32_t> cap)
{
    for (GraphRecord& record : Manifest_.Graphs)
    {
        if (record.Id != graph)
            continue;
        record.Streaming.ResidentZoneCap = cap;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

const ZoneHeader* WorldDocument::FindZoneHeader(ZoneId zone) const
{
    for (const ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id == zone)
            return &header;
    }
    return nullptr;
}

std::string WorldDocument::ResolveScenePath(std::string_view sceneRef) const
{
    namespace fs = std::filesystem;
    const fs::path ref{ std::string(sceneRef) };
    if (ref.is_absolute() || WorldPath_.empty())
        return ref.string();
    return (fs::path(WorldPath_).parent_path() / ref).string();
}

uint64_t WorldDocument::MintRawId()
{
    std::uniform_int_distribution<uint64_t> distribution;
    for (;;)
    {
        const uint64_t value = distribution(Rng_);
        if (value == 0)
            continue;
        const auto zoneHit = std::any_of(Manifest_.Zones.begin(), Manifest_.Zones.end(),
                                         [&](const ZoneHeader& z) { return z.Id.Value == value; });
        const auto graphHit = std::any_of(Manifest_.Graphs.begin(), Manifest_.Graphs.end(),
                                          [&](const GraphRecord& graph)
                                          { return graph.Id.Value == value; });
        const auto transitionHit =
            std::any_of(Manifest_.Transitions.begin(), Manifest_.Transitions.end(),
                        [&](const TransitionRecord& t) { return t.Id.Value == value; });
        bool connectionHit = false;
        if (WorldScene_ != nullptr)
        {
            const Registry& registry = WorldScene_->GetRegistry();
            for (EntityId entity : WorldScene_->GetScene().GetAllEntities())
            {
                if (const WorldDock* dock = registry.Components.TryGet<WorldDock>(entity))
                    connectionHit |= dock->Id.Value == value;
                if (const WorldLink* link = registry.Components.TryGet<WorldLink>(entity))
                    connectionHit |= link->Id.Value == value;
            }
        }
        if (zoneHit || graphHit || transitionHit || connectionHit)
            continue;
        return value;
    }
}

void WorldDocument::ApplyManifestSnapshot(WorldPartitionManifest manifest)
{
    if (!WorldMode_)
        return;

    Manifest_ = std::move(manifest);
    MarkManifestEdited();

    // Drop anything the new manifest no longer describes. Closing a zone whose
    // header is gone also drops the document a queued command could reference.
    for (auto it = OpenZones_.begin(); it != OpenZones_.end();)
    {
        if (FindZoneHeader(it->first) == nullptr)
            it = OpenZones_.erase(it);
        else
            ++it;
    }
    if (SelectedZone_.IsValid() && FindZoneHeader(SelectedZone_) == nullptr)
        SelectedZone_ = ZoneId{};

    // The focus must always land on a real zone; re-focusing raises the edited
    // document change so the editing stack rebinds.
    if (!WorldSceneFocused_ && (!FocusZone_.IsValid() || FindZoneHeader(FocusZone_) == nullptr))
    {
        FocusZone_ = ZoneId{};
        ZoneId next = Manifest_.StartZone;
        if (!next.IsValid() || FindZoneHeader(next) == nullptr)
            next = Manifest_.Zones.empty() ? ZoneId{} : Manifest_.Zones.front().Id;
        if (next.IsValid())
            (void)SetFocusZone(next);
        else
            FocusWorldScene();
    }

    RunValidation();
}

void WorldDocument::MarkManifestEdited()
{
    WorldDirty_ = true;
    IndexDirty_ = true;
}

void WorldDocument::AssignSceneRefsForNewZones()
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (!header.SceneRef.empty())
            continue;

        const std::string base = SanitizeZoneFileName(header.Name);
        std::string candidate = "levels/" + base + ".sscene";
        for (int suffix = 2;; ++suffix)
        {
            const auto taken = candidate == Manifest_.WorldSceneRef
                || std::any_of(Manifest_.Zones.begin(), Manifest_.Zones.end(),
                               [&](const ZoneHeader& other)
                               { return &other != &header && other.SceneRef == candidate; });
            if (!taken)
                break;
            candidate = "levels/" + base + "_" + std::to_string(suffix) + ".sscene";
        }
        header.SceneRef = std::move(candidate);
        MarkManifestEdited();
    }
}

void WorldDocument::RunValidation()
{
    ValidationRecords_.clear();
    if (!WorldMode_)
        return;

    ValidationRecords_ = ValidateWorldPartitionManifest(Manifest_, Index());

    std::vector<DockId> dockIds;
    std::vector<LinkId> linkIds;
    if (WorldScene_ != nullptr)
    {
        const EditorScene& scene = WorldScene_->GetScene();
        const Registry& registry = WorldScene_->GetRegistry();
        for (EntityId entity : scene.GetAllEntities())
        {
            if (const WorldDock* dock = registry.Components.TryGet<WorldDock>(entity))
            {
                const LocalTransform* transform =
                    registry.Components.TryGet<LocalTransform>(entity);
                const bool duplicate = std::find(dockIds.begin(), dockIds.end(), dock->Id)
                    != dockIds.end();
                dockIds.push_back(dock->Id);
                const auto addDockError = [&](std::string_view rule, std::string message)
                {
                    ValidationRecords_.push_back({
                        .Severity = ContentRiskSeverity::Error,
                        .Kind = ContentRiskSourceKind::Dock,
                        .SourceId = dock->Id.Value,
                        .RuleId = std::string(rule),
                        .Message = std::move(message),
                    });
                };
                if (!dock->Id.IsValid())
                    addDockError("partition.dock.id_invalid", "world dock id is invalid");
                if (duplicate)
                    addDockError("partition.dock.id_duplicate",
                        std::format("duplicate world dock id {}", DockIdToString(dock->Id)));
                const bool zoneAKnown = FindZoneHeader(dock->ZoneA) != nullptr;
                const bool zoneBKnown = FindZoneHeader(dock->ZoneB) != nullptr;
                if (!zoneAKnown || !zoneBKnown)
                    addDockError("partition.dock.zone_unresolved",
                        std::format("world dock {} references an unresolved zone",
                                    DockIdToString(dock->Id)));
                if (dock->ZoneA == dock->ZoneB)
                    addDockError("partition.dock.same_zone",
                        std::format("world dock {} connects a zone to itself",
                                    DockIdToString(dock->Id)));
                const bool planeValid = std::isfinite(dock->HalfExtents.X)
                    && std::isfinite(dock->HalfExtents.Y)
                    && dock->HalfExtents.X > 0.0f && dock->HalfExtents.Y > 0.0f;
                if (!planeValid)
                    addDockError("partition.dock.plane_degenerate",
                        std::format("world dock {} has non-positive plane dimensions",
                                    DockIdToString(dock->Id)));
                if (dock->Directions < 1u || dock->Directions > 3u)
                    addDockError("partition.dock.semantics_invalid",
                        std::format("world dock {} has invalid directionality",
                                    DockIdToString(dock->Id)));

                bool transformValid = transform != nullptr;
                bool unitScale = false;
                if (transform != nullptr)
                {
                    const Transform3f& value = transform->Value;
                    const auto finite = [](Vec3d vector)
                    { return std::isfinite(vector.X) && std::isfinite(vector.Y)
                        && std::isfinite(vector.Z); };
                    const Vec3d right = value.Right();
                    const Vec3d up = value.Up();
                    const Vec3d normal = -value.Forward();
                    transformValid = finite(value.Position) && finite(right)
                        && finite(up) && finite(normal)
                        && std::abs(right.SqrMagnitude() - 1.0f) <= 1e-3f
                        && std::abs(up.SqrMagnitude() - 1.0f) <= 1e-3f
                        && std::abs(normal.SqrMagnitude() - 1.0f) <= 1e-3f
                        && std::abs(right.Dot(up)) <= 1e-3f
                        && std::abs(right.Dot(normal)) <= 1e-3f
                        && std::abs(up.Dot(normal)) <= 1e-3f;
                    unitScale = std::abs(value.Scale.X - 1.0f) <= 1e-4f
                        && std::abs(value.Scale.Y - 1.0f) <= 1e-4f
                        && std::abs(value.Scale.Z - 1.0f) <= 1e-4f;
                }
                if (!transformValid)
                    addDockError("partition.dock.transform_invalid",
                        std::format("world dock {} has a missing or invalid plane transform",
                                    DockIdToString(dock->Id)));
                if (transform != nullptr && !unitScale)
                    addDockError("partition.dock.scale_not_unit",
                        std::format("world dock {} must use unit entity scale",
                                    DockIdToString(dock->Id)));

                if (!zoneAKnown || !zoneBKnown || dock->ZoneA == dock->ZoneB
                    || !transformValid)
                    continue;
                const Vec3d normal = (-transform->Value.Forward()).Normalized();
                const ZoneId probeA = UniqueZoneAt(
                    Manifest_, transform->Value.Position - normal);
                const ZoneId probeB = UniqueZoneAt(
                    Manifest_, transform->Value.Position + normal);
                if (probeA != dock->ZoneA || probeB != dock->ZoneB)
                {
                    ValidationRecords_.push_back({
                        .Severity = ContentRiskSeverity::Warning,
                        .Kind = ContentRiskSourceKind::Dock,
                        .SourceId = dock->Id.Value,
                        .RuleId = "partition.dock.probe_mismatch",
                        .Message = std::format("world dock {} side probes disagree with explicit zones",
                                               DockIdToString(dock->Id)),
                    });
                }
            }
            if (const WorldLink* link = registry.Components.TryGet<WorldLink>(entity))
            {
                const bool duplicate = std::find(linkIds.begin(), linkIds.end(), link->Id)
                    != linkIds.end();
                linkIds.push_back(link->Id);
                const bool zonesKnown = FindZoneHeader(link->ZoneA) != nullptr
                    && FindZoneHeader(link->ZoneB) != nullptr
                    && link->ZoneA != link->ZoneB;
                if (!link->Id.IsValid() || duplicate || !zonesKnown
                    || link->Kind != LinkKind::Teleport
                    || link->Directions < 1u || link->Directions > 3u)
                {
                    ValidationRecords_.push_back({
                        .Severity = ContentRiskSeverity::Error,
                        .Kind = ContentRiskSourceKind::Link,
                        .SourceId = link->Id.Value,
                        .RuleId = "partition.link.authored_invalid",
                        .Message = std::format("world link {} has invalid identity, zones, or semantics",
                                               LinkIdToString(link->Id)),
                    });
                }
            }
        }

    }

    const auto validateBindings = [&](const Registry& registry)
    {
        for (EntityId entity : registry.Components.GetAliveEntities())
        {
            const DockGateBinding* binding =
                registry.Components.TryGet<DockGateBinding>(entity);
            if (binding == nullptr
                || std::find(dockIds.begin(), dockIds.end(), binding->Id) != dockIds.end())
                continue;
            ValidationRecords_.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Dock,
                .SourceId = binding->Id.Value,
                .RuleId = "partition.gate_binding.dock_missing",
                .Message = std::format("gate binding references missing dock {}",
                                       DockIdToString(binding->Id)),
            });
        }
    };
    if (WorldScene_ != nullptr)
        validateBindings(WorldScene_->GetRegistry());
    for (const auto& entry : OpenZones_)
        validateBindings(entry.second.Document->GetRegistry());

    // The pure layer takes no filesystem; resolvability of nonempty scene refs
    // is the editor's half, reported through the same record type.
    std::vector<ContentRiskRecord> unresolved;
    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        if (zone.SceneRef.empty())
            continue;
        std::error_code ec;
        if (std::filesystem::exists(ResolveScenePath(zone.SceneRef), ec))
            continue;
        unresolved.push_back({
            .Severity = ContentRiskSeverity::Error,
            .Kind = ContentRiskSourceKind::Zone,
            .SourceId = zone.Id.Value,
            .RuleId = "partition.zone.scene_unresolved",
            .Message = std::format("zone {} scene '{}' resolves to no file",
                                   ZoneIdToString(zone.Id), zone.SceneRef),
        });
    }
    std::sort(unresolved.begin(), unresolved.end(),
              [](const ContentRiskRecord& a, const ContentRiskRecord& b)
              { return a.SourceId < b.SourceId; });
    for (ContentRiskRecord& record : unresolved)
        ValidationRecords_.push_back(std::move(record));

    // Containment of open-zone content in its coarse authored AABB,
    // one record per zone. Entities without world bounds are skipped.
    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(zone.Id);
        if (it == OpenZones_.end())
            continue;
        const EditorScene& scene = it->second.Document->GetScene();
        size_t outside = 0;
        for (EntityId entity : scene.GetAllEntities())
        {
            const auto bounds = scene.TryGetWorldBounds(entity);
            if (!bounds.has_value())
                continue;
            if (!zone.Bounds.Contains(bounds->Min) || !zone.Bounds.Contains(bounds->Max))
                ++outside;
        }
        if (outside == 0)
            continue;
        ValidationRecords_.push_back({
            .Severity = ContentRiskSeverity::Warning,
            .Kind = ContentRiskSourceKind::Zone,
            .SourceId = zone.Id.Value,
            .RuleId = "partition.zone.entity_outside_bounds",
            .Message = std::format("zone {} has {} entities outside its bounds",
                                   ZoneIdToString(zone.Id), outside),
        });
    }

    auto& log = Logging_.GetLogger<WorldDocument>();
    for (const ContentRiskRecord& record : ValidationRecords_)
    {
        if (record.Severity == ContentRiskSeverity::Error)
            log.Error("{}: {}", record.RuleId, record.Message);
    }
}

void WorldDocument::CreateWorldSceneDocument()
{
    WorldScene_ = std::make_unique<EditorDocument>(Logging_);
    WorldScene_->OnEdited = [this] { Revalidate(); };
    WorldScene_->SetRegistryIdentity(RegistryId{ NextRegistryIndex_++, 1 }, ZoneId{});
    if (Assets_)
        WorldScene_->SetAssetEnvironment(*Assets_);
    if (Manifest_.WorldSceneRef.empty())
        return;

    // A referenced scene that fails to load degrades to an empty world scene:
    // the world stays editable, and the next save rewrites the file.
    const std::string scenePath = ResolveScenePath(Manifest_.WorldSceneRef);
    if (!WorldScene_->Load(scenePath))
    {
        Logging_.GetLogger<WorldDocument>().Error(
            "cannot load world scene '{}'; the world scene starts empty", scenePath);
        WorldScene_ = std::make_unique<EditorDocument>(Logging_);
        WorldScene_->OnEdited = [this] { Revalidate(); };
        WorldScene_->SetRegistryIdentity(RegistryId{ NextRegistryIndex_++, 1 }, ZoneId{});
        if (Assets_)
            WorldScene_->SetAssetEnvironment(*Assets_);
    }
}

std::string WorldDocument::UserSidecarPath() const
{
    return WorldPath_ + ".user.json";
}

void WorldDocument::WriteUserSidecar() const
{
    if (!WorldMode_ || WorldPath_.empty())
        return;

    JsonValue::Object root;
    if (FocusZone_.IsValid())
        root.emplace_back("focus_zone", JsonValue{ ZoneIdToString(FocusZone_) });
    if (WorldSceneFocused_)
        root.emplace_back("focus_world_scene", JsonValue{ true });
    if (ViewSettings_ != nullptr)
    {
        root.emplace_back("show_zone_bounds", JsonValue{ ViewSettings_->ShowZoneBounds });
        root.emplace_back("streaming_preview", JsonValue{ ViewSettings_->StreamingPreview });
        // Preview overrides follow the absent-inherits model on disk too: no
        // key means the preview shows the authored per-graph shape.
        if (ViewSettings_->PreviewHopCount)
            root.emplace_back("preview_hop_count", JsonValue{ *ViewSettings_->PreviewHopCount });
    }

    JsonValue::Array zones;
    for (const ZoneHeader& header : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(header.Id);
        if (it == OpenZones_.end())
            continue;
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ ZoneIdToString(header.Id) });
        entry.emplace_back("open", JsonValue{ true });
        entry.emplace_back("visible", JsonValue{ it->second.View.VisibleInEditor });
        zones.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("zones", JsonValue{ std::move(zones) });

    std::ofstream file(UserSidecarPath(), std::ios::binary | std::ios::trunc);
    if (file.is_open())
        file << JsonStringify(JsonValue{ std::move(root) }, /*pretty*/ true);
}

WorldDocument::SidecarFocus WorldDocument::ApplyUserSidecar()
{
    std::ifstream file(UserSidecarPath(), std::ios::binary);
    if (!file.is_open())
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();

    // A malformed or missing sidecar is silently ignored: it is per-user view
    // state, never content, so the defaults (start zone focused, nothing else
    // open) are always an acceptable outcome.
    const std::optional<JsonValue> root = JsonParse(buffer.str());
    if (!root.has_value() || !root->IsObject())
        return {};

    if (const JsonValue* zones = root->Find("zones"); zones != nullptr && zones->IsArray())
    {
        for (const JsonValue& entry : zones->AsArray())
        {
            const JsonValue* id = entry.Find("id");
            if (id == nullptr || !id->IsString())
                continue;
            const auto zone = ZoneIdFromString(id->AsString());
            if (!zone.has_value() || FindZoneHeader(*zone) == nullptr)
                continue;
            const JsonValue* open = entry.Find("open");
            if (open == nullptr || !open->IsBool() || !open->AsBool())
                continue;
            if (!LoadZone(*zone))
                continue;
            if (const JsonValue* visible = entry.Find("visible");
                visible != nullptr && visible->IsBool())
                SetZoneVisible(*zone, visible->AsBool());
        }
    }

    if (const JsonValue* show = root->Find("show_zone_bounds");
        show != nullptr && show->IsBool() && ViewSettings_ != nullptr)
        ViewSettings_->ShowZoneBounds = show->AsBool();
    if (const JsonValue* preview = root->Find("streaming_preview");
        preview != nullptr && preview->IsBool() && ViewSettings_ != nullptr)
        ViewSettings_->StreamingPreview = preview->AsBool();
    if (const JsonValue* hops = root->Find("preview_hop_count");
        hops != nullptr && hops->IsNumber() && ViewSettings_ != nullptr)
        ViewSettings_->PreviewHopCount = std::max(0, static_cast<int>(hops->AsNumber()));

    SidecarFocus result;
    if (const JsonValue* focus = root->Find("focus_zone"); focus != nullptr && focus->IsString())
    {
        if (const auto zone = ZoneIdFromString(focus->AsString()); zone.has_value())
            result.Zone = *zone;
    }
    if (const JsonValue* worldScene = root->Find("focus_world_scene");
        worldScene != nullptr && worldScene->IsBool())
        result.WorldScene = worldScene->AsBool();
    return result;
}

void WorldDocument::NotifyWillReplaceDocuments()
{
    if (OnWillReplaceDocuments)
        OnWillReplaceDocuments();
}

void WorldDocument::NotifyEditedDocumentChanged()
{
    if (OnEditedDocumentChanged)
        OnEditedDocumentChanged();
}

void WorldDocument::CloseWorldState()
{
    WriteUserSidecar();
    WorldMode_ = false;
    WorldPath_.clear();
    Manifest_ = {};
    IndexDirty_ = true;
    WorldDirty_ = false;
    FocusZone_ = ZoneId{};
    SelectedZone_ = ZoneId{};
    WorldSceneFocused_ = false;
    WorldScene_.reset();
    OpenZones_.clear();
    ValidationRecords_.clear();
}

void WorldDocument::CloseWorldToLegacy()
{
    CloseWorldState();
    LegacyDocument_ = std::make_unique<EditorDocument>(Logging_);
    if (Assets_)
        LegacyDocument_->SetAssetEnvironment(*Assets_);
}
