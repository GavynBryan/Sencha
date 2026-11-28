#include "authoring/EntityReferences.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityLabel.h"
#include "document/WorldDocument.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"

#include <ecs/Query.h>
#include <ecs/World.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <format>
#include <memory>
#include <tuple>

namespace
{
constexpr std::string_view kWorldScenePlace = "World scene";
// Hex digits of an identity shown to tell same-named candidates apart.
constexpr std::size_t kShortIdentity = 6;

struct LoadedDocument
{
    const EditorDocument* Document = nullptr;
    ZoneId Zone;
    bool WorldScene = false;
};

// Manifest order, then the world scene: the order a reference is looked for in.
std::vector<LoadedDocument> LoadedDocuments(WorldDocument& world)
{
    std::vector<LoadedDocument> loaded;
    world.VisitOpenZones([&](ZoneId zone, EditorDocument& document, const ZoneViewState&) {
        loaded.push_back({ .Document = &document, .Zone = zone, .WorldScene = false });
    });
    if (world.IsWorld())
        loaded.push_back({ .Document = &world.WorldSceneDocument(), .Zone = {}, .WorldScene = true });
    return loaded;
}

std::vector<LoadedDocument> DocumentsIn(WorldDocument& world, const EntityReferenceFilter& filter)
{
    std::vector<LoadedDocument> loaded = LoadedDocuments(world);
    if (filter.Within == nullptr)
        return loaded;
    const auto within = std::ranges::find(loaded, filter.Within, &LoadedDocument::Document);
    if (within != loaded.end())
        return { *within };
    // A document the world does not hold has no place to name.
    return { LoadedDocument{ .Document = filter.Within, .Zone = {}, .WorldScene = false } };
}

std::string PlaceOf(const WorldDocument& world, const LoadedDocument& document)
{
    if (document.WorldScene)
        return std::string(kWorldScenePlace);
    if (!world.IsWorld() || !document.Zone.IsValid())
        return {};
    return ZoneDisplayName(world.Manifest(), document.Zone);
}

bool Admits(const EntityReferenceFilter& filter, const EntityReferenceSubject& subject)
{
    return !filter.Admits || filter.Admits(subject);
}

// Two placements of one prefab in one zone read alike: each sorted run of them
// carries the tail of its identity in its note.
void TellApartSameNamed(std::vector<EntityReferenceChoice>& sorted)
{
    for (std::size_t first = 0; first < sorted.size();)
    {
        std::size_t last = first + 1;
        while (last < sorted.size() && sorted[last].Label == sorted[first].Label
               && sorted[last].Place == sorted[first].Place)
            ++last;
        if (last - first > 1)
            for (std::size_t i = first; i < last; ++i)
            {
                const std::string& value = sorted[i].Value;
                const std::string_view tail = std::string_view(value).substr(value.size() - kShortIdentity);
                sorted[i].Place = sorted[i].Place.empty() ? std::format("#{}", tail)
                                                          : std::format("{} \xC2\xB7 #{}", sorted[i].Place, tail);
            }
        first = last;
    }
}

std::string Placed(std::string name, const std::string& place)
{
    return place.empty() ? name : std::format("{} \xC2\xB7 {}", name, place);
}
} // namespace

std::vector<EntityReferenceChoice> EntityReferenceCandidates(WorldDocument& world, const EntityReferenceFilter& filter)
{
    std::vector<EntityReferenceChoice> choices;
    for (const LoadedDocument& document : DocumentsIn(world, filter))
    {
        const World& components = std::as_const(document.Document->GetScene().GetRegistry()).Components;
        if (!components.IsRegistered<PersistentIdComponent>())
            continue;
        const std::string place = PlaceOf(world, document);
        Query<Read<PersistentIdComponent>> identified(components);
        identified.ForEachChunk([&](auto& view) {
            const auto ids = view.template Read<PersistentIdComponent>();
            for (std::uint32_t row = 0; row < view.Count(); ++row)
            {
                const EntityId entity = view.Entity(row);
                if (!ids[row].Id.IsValid()
                    || !Admits(filter, { .Document = *document.Document, .Components = components, .Entity = entity }))
                    continue;
                choices.push_back({ .Value = PersistentEntityIdToString(ids[row].Id),
                                    .Label = EntityLabel(*document.Document, entity),
                                    .Place = place });
            }
        });
    }
    std::ranges::sort(choices, [](const EntityReferenceChoice& a, const EntityReferenceChoice& b) {
        return std::tie(a.Label, a.Place, a.Value) < std::tie(b.Label, b.Place, b.Value);
    });
    TellApartSameNamed(choices);
    return choices;
}

std::optional<EntityReferenceSubject> AdmittedEntityReference(WorldDocument& world,
                                                              const EntityReferenceFilter& filter,
                                                              PersistentEntityId target)
{
    if (!target.IsValid())
        return std::nullopt;
    for (const LoadedDocument& document : DocumentsIn(world, filter))
    {
        const EditorScene& scene = document.Document->GetScene();
        const EntityReferenceSubject subject{ .Document = *document.Document,
                                              .Components = scene.GetRegistry().Components,
                                              .Entity = scene.EntityWithPersistentId(target) };
        if (subject.Entity.IsValid() && Admits(filter, subject))
            return subject;
    }
    return std::nullopt;
}

EntityReferenceReading ReadEntityReference(WorldDocument& world, std::string_view value, ZoneId expected)
{
    EntityReferenceReading reading;
    if (value.empty())
    {
        reading.Text = "None";
        return reading;
    }
    const std::optional<PersistentEntityId> target = PersistentEntityIdFromString(value);
    if (!target.has_value())
    {
        reading.State = EntityReferenceState::Malformed;
        reading.Text = std::format("Not an identity: {}", value);
        return reading;
    }
    reading.Target = *target;
    for (const LoadedDocument& document : LoadedDocuments(world))
    {
        const EntityId entity = document.Document->GetScene().EntityWithPersistentId(*target);
        if (!entity.IsValid())
            continue;
        reading.State = EntityReferenceState::Loaded;
        reading.Document = document.Document;
        reading.Entity = entity;
        reading.InWorldScene = document.WorldScene;
        reading.Zone = document.Zone;
        reading.Text = Placed(EntityLabel(*document.Document, entity), PlaceOf(world, document));
        return reading;
    }
    const std::string id = PersistentEntityIdToString(*target);
    if (world.IsWorld() && expected.IsValid() && !world.IsZoneOpen(expected))
    {
        reading.State = EntityReferenceState::NotLoaded;
        reading.Zone = expected;
        reading.Text = std::format("In {}, not loaded", ZoneDisplayName(world.Manifest(), expected));
        return reading;
    }
    const bool someZoneClosed =
        world.IsWorld() && std::ranges::any_of(world.Manifest().Zones, [&](const ZoneHeader& zone) {
            return !world.IsZoneOpen(zone.Id);
        });
    reading.State = someZoneClosed ? EntityReferenceState::Unlocated : EntityReferenceState::Missing;
    reading.Text = someZoneClosed ? std::format("Not in a loaded zone \xC2\xB7 {}", id)
                                  : std::format("Missing \xC2\xB7 {}", id);
    return reading;
}

void ShowEntityReference(PropertyNode& node, const EntityReferenceReading& reading)
{
    node.Reading = reading.Text;
    if (reading.State == EntityReferenceState::Loaded)
        node.Descriptor.Verbs.push_back({ .Id = std::string(EntityReferenceVerbs::Select), .Label = "Select" });
}

void ShowEntityReferences(std::vector<PropertyNode>& nodes, WorldDocument& world)
{
    for (PropertyNode& node : nodes)
    {
        if (node.Descriptor.Kind == PropertyKind::EntityReference && node.Reading.empty())
            ShowEntityReference(node, ReadEntityReference(world, node.Value));
        ShowEntityReferences(node.Children, world);
    }
}

bool SelectEntityReference(WorldDocument& world, SelectionService& selection, CommandStack& commands,
                           std::string_view value)
{
    const EntityReferenceReading reading = ReadEntityReference(world, value);
    if (reading.State != EntityReferenceState::Loaded)
        return false;
    if (reading.Document != &world.FocusDocument())
    {
        const bool focused = reading.InWorldScene ? world.FocusWorldScene() : world.SetFocusZone(reading.Zone);
        if (!focused)
            return false;
    }
    const EditorScene& scene = world.FocusDocument().GetScene();
    const EntityId entity = scene.EntityWithPersistentId(reading.Target);
    if (!entity.IsValid())
        return false;
    commands.Execute(
        std::make_unique<SelectCommand>(selection, SelectableRef::EntitySelection(scene.GetRegistry().Id, entity)));
    return true;
}
