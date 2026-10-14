#include "authoring/ComponentPropertySource.h"

#include "authoring/EditorComponentAdapter.h"
#include "authoring/RuntimeFieldText.h"
#include "commands/CommandStack.h"
#include "commands/CompositeCommand.h"
#include "document/AssetFieldIo.h"
#include "document/DerivedComponents.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "document/commands/AssetFieldEditCommand.h"
#include "document/commands/RawComponentAddCommand.h"
#include "document/commands/RawComponentEditCommand.h"
#include "document/commands/RawComponentRemoveCommand.h"
#include "properties/SettingProperties.h"
#include "selection/SelectionService.h"

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <format>
#include <memory>
#include <unordered_set>
#include <utility>

namespace
{
// What adding `component` to `entity` would put there besides itself, as a
// person reads it: nothing, or how many and which.
std::string BroughtWith(const World& world, EntityId entity, ComponentId component)
{
    std::vector<std::string> names;
    for (const ComponentId owed : DerivedClosureOf(world, component))
        if (const ComponentMeta* meta = world.GetMeta(owed); meta != nullptr && !world.HasComponent(entity, owed))
            names.push_back(HumanizeSchemaName(meta->Name));
    if (names.empty())
        return {};
    std::string note = std::format("Also adds {}: ", names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
        note += (i == 0 ? "" : ", ") + names[i];
    return note;
}

PropertyKind KindOf(const RuntimeField& field)
{
    if (field.Asset != AssetType::Unknown)
        return PropertyKind::Asset;
    if (field.InlineText)
        return PropertyKind::Text;
    if (!IsRuntimeFieldEditable(field) && !field.ReadOnly)
        return PropertyKind::Unsupported;
    if (!field.Enum.empty())
        return PropertyKind::Choice;
    if (field.Scalar == FieldScalar::Bool)
        return PropertyKind::Toggle;
    if (field.Scalar == FieldScalar::Color3)
        return PropertyKind::Color;
    return field.Count > 1 ? PropertyKind::Vector : PropertyKind::Number;
}

std::string FieldKey(std::string_view component, const RuntimeField& field)
{
    return std::string(component) + "/" + field.Name;
}

constexpr char kAdapterSeparator = ':';

void PrefixKeys(std::vector<PropertyNode>& nodes, const std::string& prefix)
{
    for (PropertyNode& node : nodes)
    {
        node.Key = prefix + node.Key;
        PrefixKeys(node.Children, prefix);
    }
}

bool Overridden(const RuntimeField& field, const std::byte* live, const std::vector<std::byte>& baseline)
{
    if (live == nullptr || field.Asset != AssetType::Unknown || field.ReadOnly)
        return false;
    const std::size_t span = field.Size * field.Count;
    if (field.Offset + span > baseline.size())
        return false;
    return std::memcmp(live + field.Offset, baseline.data() + field.Offset, span) != 0;
}

// The node shows the holders' value where they agree, and stands for all of
// them where they do not.
void Agree(PropertyNode& node, const std::vector<std::string>& values)
{
    if (values.empty())
        return;
    node.Mixed = std::ranges::any_of(values, [&](const std::string& value) { return value != values.front(); });
    node.Value = node.Mixed ? std::string{} : values.front();
}

// Whether every holder reads as the first does, for the whole field or one of
// its numbers. Equal bytes read alike; bytes that differ may still read alike
// (0 and -0), so only those are read, stopping at the first that differs.
bool ReadAlike(const RuntimeField& field, const std::vector<const std::byte*>& bytes,
               std::optional<std::size_t> element)
{
    // An angle of a rotation is not one of its bytes' scalars: only the whole
    // quaternion compares.
    const bool whole = !element.has_value() || field.DisplayAngles;
    const std::size_t offset = field.Offset + (whole ? 0 : *element * field.Size);
    const std::size_t span = whole ? field.Size * field.Count : field.Size;
    const auto read = [&](const std::byte* held) {
        return element.has_value() ? FormatRuntimeFieldElement(field, held, *element) : FormatRuntimeField(field, held);
    };
    std::optional<std::string> first;
    for (std::size_t i = 1; i < bytes.size(); ++i)
    {
        if (std::memcmp(bytes[i] + offset, bytes.front() + offset, span) == 0)
            continue;
        if (!first.has_value())
            first = read(bytes.front());
        if (read(bytes[i]) != *first)
            return false;
    }
    return true;
}

// The node shows what the holders read as where they agree, and stands for
// all of them where they do not.
void Show(PropertyNode& node, const RuntimeField& field, const std::vector<const std::byte*>& bytes,
          std::optional<std::size_t> element)
{
    node.Mixed = !ReadAlike(field, bytes, element);
    if (!node.Mixed)
        node.Value = element.has_value() ? FormatRuntimeFieldElement(field, bytes.front(), *element)
                                         : FormatRuntimeField(field, bytes.front());
}

// A vector's numbers, named: axes, or the pitch, yaw and roll of a rotation.
std::vector<std::string> ElementLabels(const RuntimeField& field)
{
    if (field.DisplayAngles)
        return { "P", "Y", "R" };
    static constexpr std::array<std::string_view, 4> kAxes = { "X", "Y", "Z", "W" };
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < field.Count; ++i)
        labels.push_back(field.Count <= kAxes.size() ? std::string(kAxes[i]) : std::to_string(i));
    return labels;
}

bool WriteValue(const RuntimeField& leaf, std::optional<std::size_t> element, std::string_view value, std::byte* bytes)
{
    return element.has_value() ? ParseRuntimeFieldElement(leaf, *element, value, bytes)
                               : ParseRuntimeField(leaf, value, bytes);
}

bool WriteOffset(const RuntimeField& leaf, std::optional<std::size_t> element, double delta, std::byte* bytes)
{
    // A whole vector has no one number to move.
    if (!element.has_value() && leaf.Count > 1)
        return false;
    return OffsetRuntimeFieldElement(leaf, element.value_or(0), delta, bytes);
}
} // namespace

ComponentPropertySource::ComponentPropertySource(WorldDocument& world,
                                                 SelectionService& selection,
                                                 CommandStack& commands,
                                                 const EditorComponentAdapterRegistry& adapters)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , Adapters(adapters)
{
}

ComponentPropertySource::~ComponentPropertySource()
{
    if (Phase != EditPhase::Idle)
        CancelEdit(EditingKey);
}

EntityId ComponentPropertySource::Entity() const
{
    const SelectableRef selection = Selection.GetPrimarySelection();
    if (!selection.IsEntity() || !WorldDoc.FocusDocument().GetScene().HasEntity(selection.Entity))
        return {};
    return selection.Entity;
}

std::vector<EntityId> ComponentPropertySource::Entities() const
{
    std::vector<EntityId> entities;
    const EntityId primary = Entity();
    if (!primary.IsValid())
        return entities;
    entities.push_back(primary);
    const EditorScene& scene = WorldDoc.FocusDocument().GetScene();
    std::unordered_set<EntityId, EntityIdHash> seen{ primary };
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity() && scene.HasEntity(ref.Entity) && seen.insert(ref.Entity).second)
            entities.push_back(ref.Entity);
    return entities;
}

void ComponentPropertySource::ShowElsewhere(std::vector<ComponentTypeId> types)
{
    ShownElsewhere = std::move(types);
}

std::string ComponentPropertySource::KeyOf(ComponentTypeId type, std::string_view field) const
{
    const IComponentSerializer* serializer = EditorSceneSerializers().FindByType(type);
    if (serializer == nullptr)
        return {};
    for (const RuntimeField& leaf : serializer->RuntimeFields())
        if (leaf.Name == field)
            return FieldKey(serializer->JsonKey(), leaf);
    return {};
}

std::vector<PropertyNode> ComponentPropertySource::Nodes() const
{
    std::vector<PropertyNode> nodes;
    const std::vector<EntityId> entities = Entities();
    if (entities.empty())
        return nodes;

    EditorDocument& document = WorldDoc.FocusDocument();
    const World& world = std::as_const(document.GetScene().GetRegistry()).Components;
    AssetSystem* assets = document.GetAssetSystem();
    if (BaselinesFor != entities)
    {
        Baselines.clear();
        BaselinesFor = entities;
    }

    std::vector<std::string> addable;
    std::vector<std::string> addableNotes;
    std::vector<std::string> addableKeys;
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        if (id == InvalidComponentId)
            continue;
        const std::string key(serializer->JsonKey());
        std::vector<EntityId> holders;
        for (const EntityId entity : entities)
            if (world.HasComponent(entity, id))
                holders.push_back(entity);
        const bool elsewhere = std::ranges::find(ShownElsewhere, serializer->TypeId()) != ShownElsewhere.end();
        if (elsewhere && !holders.empty())
            continue;
        if (holders.empty())
        {
            // A component the document owns (one that may not be removed) is
            // not the editor's to add either: its managed state would never be
            // minted.
            if (serializer->IsRemovable())
            {
                addable.push_back(HumanizeSchemaName(key));
                addableKeys.push_back(key);
                addableNotes.push_back(BroughtWith(world, entities.front(), id));
            }
            continue;
        }

        const std::size_t lacking = entities.size() - holders.size();
        // Closed until opened: a component's fields are read when it is the one being worked on.
        PropertyNode group{ .Key = key,
                            .Descriptor = { .Kind = PropertyKind::Group,
                                            .Label = lacking == 0 ? HumanizeSchemaName(key)
                                                                  : std::format("{} \xC2\xB7 on {} of {}",
                                                                                HumanizeSchemaName(key),
                                                                                holders.size(), entities.size()),
                                            .Folded = true } };
        if (serializer->IsRemovable())
        {
            group.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove Component", .Removes = true });
            if (lacking > 0)
                group.Descriptor.Verbs.push_back({ .Id = "spread", .Label = std::format("Add to the other {}", lacking) });
        }

        const IEditorComponentAdapter* adapter = Adapters.Find(serializer->TypeId());
        if (adapter != nullptr)
            group.Descriptor.Actions.assign(adapter->Actions().begin(), adapter->Actions().end());
        if (adapter != nullptr && adapter->AuthorsInspectorRows())
        {
            // An adapter's rows describe one object; several together have no
            // one stack or list to show.
            if (holders.size() == 1)
            {
                group.Children = adapter->InspectorNodes(InspectorContext(holders.front()));
                PrefixKeys(group.Children, key + kAdapterSeparator);
            }
            else
                group.Children.push_back(NoteProperty(key + kAdapterSeparator + "together", "",
                                                      "Each object's own: select one to change it."));
            nodes.push_back(std::move(group));
            continue;
        }

        const std::span<const RuntimeField> fields = serializer->RuntimeFields();
        // A zero-size tag has no storage, so bytes are asked for only when there
        // is a leaf to read; the group still appears, fields or not.
        std::vector<const std::byte*> bytes;
        std::vector<const std::vector<std::byte>*> baselines;
        for (const EntityId holder : holders)
        {
            bytes.push_back(fields.empty() ? nullptr
                                           : static_cast<const std::byte*>(world.GetComponentRaw(holder, id)));
            baselines.push_back(document.IsSceneInstanceMember(holder) ? &Baseline(holder, *serializer, id) : nullptr);
        }
        if (!fields.empty() && std::ranges::find(bytes, nullptr) != bytes.end())
            continue;
        if (holders.size() == 1 && baselines.front() != nullptr && baselines.front()->empty())
            group.Descriptor.Tooltip = "Added to this instance; the source has no such component.";
        bool overridden = false;
        bool holdsAssets = false;

        for (const RuntimeField& field : fields)
        {
            PropertyNode node{
                .Key = FieldKey(key, field),
                .Descriptor = { .Kind = KindOf(field),
                                .Label = RuntimeFieldLabel(field),
                                .Tooltip = std::string(field.Tooltip),
                                .Asset = field.Asset,
                                .Step = field.DisplayDegrees ? 1.0 : 0.0,
                                .ReadOnly = field.ReadOnly || !IsRuntimeFieldEditable(field) },
            };
            for (const EnumOption& option : field.Enum)
                node.Descriptor.Choices.emplace_back(option.Name);
            holdsAssets |= field.Asset != AssetType::Unknown;

            if (field.Asset == AssetType::Unknown)
            {
                if (node.Descriptor.Kind != PropertyKind::Unsupported)
                    Show(node, field, bytes, std::nullopt);
                // A vector's numbers are its children, so one can change on its own.
                if (node.Descriptor.Kind == PropertyKind::Vector)
                {
                    const std::vector<std::string> labels = ElementLabels(field);
                    for (std::size_t element = 0; element < labels.size(); ++element)
                    {
                        PropertyNode part{ .Key = std::format("{}[{}]", node.Key, element),
                                           .Descriptor = { .Kind = PropertyKind::Number,
                                                           .Label = labels[element],
                                                           .Step = field.DisplayAngles ? 1.0 : 0.0,
                                                           .ReadOnly = node.Descriptor.ReadOnly } };
                        Show(part, field, bytes, element);
                        node.Children.push_back(std::move(part));
                    }
                }
                bool differs = false;
                for (std::size_t i = 0; i < holders.size(); ++i)
                    differs |= baselines[i] != nullptr && Overridden(field, bytes[i], *baselines[i]);
                if (differs)
                {
                    node.Descriptor.Verbs.push_back({ .Id = "reset", .Label = "Reset to Source" });
                    overridden = true;
                }
                group.Children.push_back(std::move(node));
                continue;
            }

            // Asset handles are chosen, never typed, and a list of them is one
            // property per slot so each slot can be chosen on its own.
            node.Descriptor.ReadOnly = field.ReadOnly;
            if (assets == nullptr)
            {
                group.Children.push_back(std::move(node));
                continue;
            }
            std::vector<std::vector<std::string>> lists;
            for (const std::byte* held : bytes)
            {
                std::vector<std::string> paths;
                for (const AssetRef& ref : ReadAssetField(*assets, field.Asset, field.Arity, held + field.Offset).Refs)
                    paths.push_back(ref.Path);
                lists.push_back(std::move(paths));
            }
            if (field.Arity != AssetArity::List)
            {
                std::vector<std::string> values;
                for (const std::vector<std::string>& paths : lists)
                    values.push_back(paths.empty() ? std::string{} : paths.front());
                Agree(node, values);
                group.Children.push_back(std::move(node));
                continue;
            }
            // Lists that differ have no slots in common to show: the first
            // stands for them, and choosing it sets each one's first.
            const bool sameLists = std::ranges::all_of(lists, [&](const auto& paths) { return paths == lists.front(); });
            const std::vector<std::string>& shown = lists.front();
            // A list with no slots still offers one, so an empty field can be filled.
            const std::size_t slots = sameLists ? std::max<std::size_t>(shown.size(), 1) : 1;
            for (std::size_t slot = 0; slot < slots; ++slot)
            {
                PropertyNode slotNode = node;
                slotNode.Key += "#" + std::to_string(slot);
                if (!shown.empty())
                    slotNode.Descriptor.Label += " " + std::to_string(slot);
                slotNode.Mixed = !sameLists;
                slotNode.Value = sameLists && slot < shown.size() ? shown[slot] : std::string{};
                group.Children.push_back(std::move(slotNode));
            }
        }
        // A whole-component reset is a blind byte copy, which a session-local
        // asset handle must never travel through.
        if (overridden && !holdsAssets)
            group.Descriptor.Verbs.push_back({ .Id = "reset", .Label = "Reset Component to Source" });
        nodes.push_back(std::move(group));
    }

    PropertyNode components{ .Key = "components", .Descriptor = { .Kind = PropertyKind::Group, .Label = "Components" } };
    if (!addable.empty())
        components.Descriptor.Verbs.push_back({ .Id = "add",
                                                .Label = "Add Component",
                                                .Choices = std::move(addable),
                                                .ChoiceNotes = std::move(addableNotes),
                                                .ChoiceKeys = std::move(addableKeys) });
    nodes.insert(nodes.begin(), std::move(components));

    // What the engine put on the entities because of what was authored:
    // always there to read, closed until opened, under what each came from.
    PropertyNode derived{ .Key = "derived",
                          .Descriptor = { .Kind = PropertyKind::Group,
                                          .Label = {},
                                          .Tooltip = "On the entity, not in the scene file: systems keep these, "
                                                     "and each goes when what it came from does.",
                                          .Folded = true } };
    std::size_t derivedCount = 0;
    for (const EntityId entity : entities)
        for (const DerivedComponentRow& row : DerivedComponentsOn(world, EditorSceneSerializers(), entity))
        {
            const ComponentMeta* owner = row.ProvidedBy == InvalidComponentId ? nullptr : world.GetMeta(row.ProvidedBy);
            const std::string source = owner != nullptr ? HumanizeSchemaName(owner->Name) : std::string("the engine");
            const std::string sourceKey = "derived/" + source;
            auto from = std::ranges::find(derived.Children, sourceKey, &PropertyNode::Key);
            if (from == derived.Children.end())
                from = derived.Children.insert(derived.Children.end(),
                                               PropertyNode{ .Key = sourceKey,
                                                             .Descriptor = { .Kind = PropertyKind::Group,
                                                                             .Label = "from " + source } });
            const std::string rowKey = "derived/" + std::string(row.Name);
            if (std::ranges::find(from->Children, rowKey, &PropertyNode::Key) != from->Children.end())
                continue;
            from->Children.push_back(PropertyNode{
                .Key = rowKey,
                .Descriptor = { .Kind = PropertyKind::Text, .Label = HumanizeSchemaName(row.Name),
                                .Tooltip = std::string(row.Name), .ReadOnly = true } });
            ++derivedCount;
        }
    if (derivedCount > 0)
    {
        derived.Descriptor.Label = std::format("Derived \xC2\xB7 {}", derivedCount);
        nodes.push_back(std::move(derived));
    }
    return nodes;
}

EditorComponentInspectorContext ComponentPropertySource::InspectorContext(EntityId entity) const
{
    EditorDocument& document = WorldDoc.FocusDocument();
    return EditorComponentInspectorContext{ .World = WorldDoc,
                                            .Document = document,
                                            .Scene = document.GetScene(),
                                            .Selection = Selection,
                                            .Commands = Commands,
                                            .Entity = entity };
}

EntityId ComponentPropertySource::SoleHolder(std::string_view adapterKey) const
{
    const IComponentSerializer* serializer =
        EditorSceneSerializers().FindByJsonKey(adapterKey.substr(0, adapterKey.find(kAdapterSeparator)));
    if (serializer == nullptr)
        return {};
    const World& world = std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components;
    const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
    EntityId holder{};
    for (const EntityId entity : Entities())
        if (id != InvalidComponentId && world.HasComponent(entity, id))
        {
            if (holder.IsValid())
                return {};
            holder = entity;
        }
    return holder;
}

const std::vector<std::byte>& ComponentPropertySource::Baseline(EntityId entity, IComponentSerializer& serializer,
                                                                ComponentId component) const
{
    const auto found = std::ranges::find_if(Baselines, [&](const BaselineBytes& baseline) {
        return baseline.Entity == entity && baseline.Component == component;
    });
    if (found != Baselines.end())
        return found->Bytes;
    return Baselines
        .emplace_back(BaselineBytes{ entity, component, WorldDoc.FocusDocument().BaselineComponentBytes(entity, serializer) })
        .Bytes;
}

std::optional<ComponentPropertySource::Field> ComponentPropertySource::Resolve(std::string_view key) const
{
    const std::size_t slash = key.find('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    const std::string_view component = key.substr(0, slash);
    std::string_view leaf = key.substr(slash + 1);
    std::size_t slot = 0;
    if (const std::size_t hash = leaf.find('#'); hash != std::string_view::npos)
    {
        const std::string_view digits = leaf.substr(hash + 1);
        if (std::from_chars(digits.data(), digits.data() + digits.size(), slot).ec != std::errc{})
            return std::nullopt;
        leaf = leaf.substr(0, hash);
    }
    std::optional<std::size_t> element;
    if (leaf.ends_with(']'))
    {
        const std::size_t open = leaf.rfind('[');
        std::size_t index = 0;
        const std::string_view digits = leaf.substr(open + 1, leaf.size() - open - 2);
        if (open == std::string_view::npos
            || std::from_chars(digits.data(), digits.data() + digits.size(), index).ec != std::errc{})
            return std::nullopt;
        element = index;
        leaf = leaf.substr(0, open);
    }

    const World& world = std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components;
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        if (serializer->JsonKey() != component)
            continue;
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        if (id == InvalidComponentId)
            return std::nullopt;
        for (const RuntimeField& field : serializer->RuntimeFields())
            if (field.Name == leaf)
            {
                if (element.has_value() && *element >= field.Count)
                    return std::nullopt;
                return Field{ serializer.get(), id, &field, slot, element };
            }
        return std::nullopt;
    }
    return std::nullopt;
}

bool ComponentPropertySource::BeginEdit(std::string_view key)
{
    // An adapter's property is set in one command when it commits; there are
    // no bytes to hold open.
    if (key.find(kAdapterSeparator) != std::string_view::npos)
        return SoleHolder(key).IsValid();
    if (Phase != EditPhase::Idle)
        CancelEdit(EditingKey);
    const std::optional<Field> field = Resolve(key);
    if (!field.has_value())
        return false;
    EditorDocument& document = WorldDoc.FocusDocument();
    const World& world = std::as_const(document.GetScene().GetRegistry()).Components;
    const ComponentMeta* meta = world.GetMeta(field->Component);
    if (meta == nullptr || meta->Size == 0)
        return false;

    Holding.clear();
    for (const EntityId entity : Entities())
        if (const void* live = world.GetComponentRaw(entity, field->Component); live != nullptr)
        {
            Held& held = Holding.emplace_back(Held{ entity, std::vector<std::byte>(meta->Size) });
            std::memcpy(held.Before.data(), live, meta->Size);
        }
    if (Holding.empty())
        return false;
    EditingKey = std::string(key);
    EditingComponent = field->Component;
    EditingDocument = &document;
    Phase = EditPhase::Armed;
    return true;
}

PropertyEditResult ComponentPropertySource::Stage(std::string_view key, const Writer& write)
{
    if (key.find(kAdapterSeparator) != std::string_view::npos)
        return PropertyEditResult::Unchanged;
    if (Phase == EditPhase::Idle || key != EditingKey)
        return PropertyEditResult::Gone;
    // The bytes held are another document's now: what began there ends there.
    if (EditingDocument != &WorldDoc.FocusDocument())
    {
        CancelEdit(key);
        return PropertyEditResult::Gone;
    }
    const std::optional<Field> field = Resolve(key);
    if (!field.has_value() || field->Leaf->Asset != AssetType::Unknown)
        return PropertyEditResult::Gone;

    // Every holder written, or none: a value one of them cannot hold changes nothing.
    std::vector<std::vector<std::byte>> staged;
    for (const Held& held : Holding)
    {
        std::vector<std::byte> working = held.Before;
        if (!write(*field, working.data()))
            return PropertyEditResult::Refused;
        staged.push_back(std::move(working));
    }
    if (Phase == EditPhase::Armed)
    {
        // Held in the command stack's one pending-edit scope from the first
        // write on, so undo, save and any interruption put the bytes back.
        Commands.OpenPendingEdit([this] { RestoreBefore(); });
        Phase = EditPhase::Previewing;
    }
    World& world = EditingDocument->GetScene().GetRegistry().Components;
    for (std::size_t i = 0; i < Holding.size(); ++i)
        if (void* live = world.GetComponentRaw(Holding[i].Entity, EditingComponent); live != nullptr)
            std::memcpy(live, staged[i].data(), staged[i].size());
    return PropertyEditResult::Applied;
}

PropertyEditResult ComponentPropertySource::Finish(std::string_view key, const Writer& write)
{
    if ((Phase == EditPhase::Idle || key != EditingKey) && !BeginEdit(key))
        return PropertyEditResult::Gone;
    const std::optional<Field> field = Resolve(key);
    if (!field.has_value())
    {
        CancelEdit(key);
        return PropertyEditResult::Gone;
    }
    std::vector<std::vector<std::byte>> afters;
    for (const Held& held : Holding)
    {
        std::vector<std::byte> after = held.Before;
        if (!write(*field, after.data()))
            return PropertyEditResult::Refused;
        afters.push_back(std::move(after));
    }
    return Conclude(std::move(afters));
}

PropertyEditResult ComponentPropertySource::Conclude(std::vector<std::vector<std::byte>> afters)
{
    if (Phase == EditPhase::Previewing)
    {
        RestoreBefore();
        Commands.ClosePendingEdit();
    }
    EditorDocument& document = *EditingDocument;
    Phase = EditPhase::Idle;
    EditingKey.clear();
    EditingDocument = nullptr;
    std::vector<Held> held = std::exchange(Holding, {});

    std::vector<std::unique_ptr<ICommand>> commands;
    for (std::size_t i = 0; i < held.size(); ++i)
        if (afters[i] != held[i].Before)
            commands.push_back(std::make_unique<RawComponentEditCommand>(held[i].Entity, EditingComponent,
                                                                         std::move(held[i].Before),
                                                                         std::move(afters[i]),
                                                                         document.GetScene(), document));
    return Run(std::move(commands));
}

PropertyEditResult ComponentPropertySource::Run(std::vector<std::unique_ptr<ICommand>> commands)
{
    if (commands.empty())
        return PropertyEditResult::Unchanged;
    if (commands.size() == 1)
        Commands.Execute(std::move(commands.front()));
    else
        Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
    return PropertyEditResult::Applied;
}

PropertyEditResult ComponentPropertySource::PreviewEdit(std::string_view key, std::string_view value)
{
    return Stage(key, [value](const Field& field, std::byte* bytes) {
        return WriteValue(*field.Leaf, field.Element, value, bytes);
    });
}

PropertyEditResult ComponentPropertySource::PreviewEditBy(std::string_view key, double delta)
{
    return Stage(key, [delta](const Field& field, std::byte* bytes) {
        return WriteOffset(*field.Leaf, field.Element, delta, bytes);
    });
}

PropertyEditResult ComponentPropertySource::CommitEdit(std::string_view key, std::string_view value)
{
    if (const std::size_t separator = key.find(kAdapterSeparator); separator != std::string_view::npos)
    {
        const EntityId holder = SoleHolder(key);
        const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, separator));
        const IEditorComponentAdapter* adapter = serializer != nullptr ? Adapters.Find(serializer->TypeId()) : nullptr;
        if (!holder.IsValid() || adapter == nullptr)
            return PropertyEditResult::Gone;
        EditorComponentInspectorContext context = InspectorContext(holder);
        return adapter->SetInspectorValue(context, key.substr(separator + 1), value);
    }
    if (const std::optional<Field> field = Resolve(key); field.has_value() && field->Leaf->Asset != AssetType::Unknown)
    {
        CancelEdit(key);
        return CommitAsset(*field, value);
    }
    return Finish(key, [value](const Field& field, std::byte* bytes) {
        return WriteValue(*field.Leaf, field.Element, value, bytes);
    });
}

PropertyEditResult ComponentPropertySource::CommitEditBy(std::string_view key, double delta)
{
    if (key.find(kAdapterSeparator) != std::string_view::npos)
        return PropertyEditResult::Refused;
    return Finish(key, [delta](const Field& field, std::byte* bytes) {
        return WriteOffset(*field.Leaf, field.Element, delta, bytes);
    });
}

void ComponentPropertySource::CancelEdit(std::string_view key)
{
    if (Phase == EditPhase::Idle || key != EditingKey)
        return;
    if (Phase == EditPhase::Previewing)
    {
        RestoreBefore();
        Commands.ClosePendingEdit();
    }
    Phase = EditPhase::Idle;
    EditingKey.clear();
    EditingDocument = nullptr;
    Holding.clear();
}

void ComponentPropertySource::CommitOpenEdit()
{
    if (Phase != EditPhase::Previewing)
    {
        CancelEdit(EditingKey);
        return;
    }
    const World& world = std::as_const(EditingDocument->GetScene().GetRegistry()).Components;
    std::vector<std::vector<std::byte>> afters;
    for (const Held& held : Holding)
    {
        const auto* live = static_cast<const std::byte*>(world.GetComponentRaw(held.Entity, EditingComponent));
        afters.push_back(live != nullptr ? std::vector<std::byte>(live, live + held.Before.size()) : held.Before);
    }
    (void)Conclude(std::move(afters));
}

PropertyEditResult ComponentPropertySource::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    const std::vector<EntityId> entities = Entities();
    if (entities.empty())
        return PropertyEditResult::Gone;
    // A verb changes structure or bytes outright, so a byte edit left open
    // would restore over it.
    if (Phase != EditPhase::Idle)
        CancelEdit(EditingKey);
    if (const std::size_t separator = key.find(kAdapterSeparator); separator != std::string_view::npos)
    {
        const EntityId holder = SoleHolder(key);
        const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, separator));
        const IEditorComponentAdapter* adapter = serializer != nullptr ? Adapters.Find(serializer->TypeId()) : nullptr;
        if (adapter == nullptr)
            return PropertyEditResult::Gone;
        if (!holder.IsValid())
            return PropertyEditResult::Refused;
        EditorComponentInspectorContext context = InspectorContext(holder);
        return adapter->ActInspector(context, key.substr(separator + 1), verb, choice);
    }
    return ActOnComponents(entities, key, verb, choice);
}

PropertyEditResult ComponentPropertySource::ActOnComponents(const std::vector<EntityId>& entities,
                                                            std::string_view key, std::string_view verb,
                                                            std::string_view choice)
{
    EditorDocument& document = WorldDoc.FocusDocument();
    World& world = document.GetScene().GetRegistry().Components;
    std::vector<std::unique_ptr<ICommand>> commands;
    const auto addTo = [&](const IComponentSerializer& serializer, ComponentId id) {
        for (const EntityId entity : entities)
            if (!world.HasComponent(entity, id))
                commands.push_back(std::make_unique<RawComponentAddCommand>(entity, id, serializer.DefaultBytes(),
                                                                            document.GetScene(), document));
    };
    if (key == "components" && verb == "add")
    {
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            if (!serializer->IsRemovable() || serializer->JsonKey() != choice)
                continue;
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId)
                return PropertyEditResult::Refused;
            addTo(*serializer, id);
            return commands.empty() ? PropertyEditResult::Refused : Run(std::move(commands));
        }
        return PropertyEditResult::Refused;
    }

    const std::size_t slash = key.find('/');
    IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, slash));
    const ComponentId id = serializer != nullptr ? world.GetComponentIdByType(serializer->TypeId()) : InvalidComponentId;
    std::vector<EntityId> holders;
    for (const EntityId entity : entities)
        if (id != InvalidComponentId && world.HasComponent(entity, id))
            holders.push_back(entity);
    if (holders.empty())
        return PropertyEditResult::Gone;

    if (slash == std::string_view::npos && (verb == "remove" || verb == "spread"))
    {
        if (!serializer->IsRemovable())
            return PropertyEditResult::Refused;
        if (verb == "spread")
            addTo(*serializer, id);
        else
            for (const EntityId holder : holders)
                commands.push_back(std::make_unique<RawComponentRemoveCommand>(holder, *serializer,
                                                                               document.GetScene(), document));
        return Run(std::move(commands));
    }
    if (verb != "reset")
        return PropertyEditResult::Gone;

    const std::optional<Field> field = slash != std::string_view::npos ? Resolve(key) : std::nullopt;
    if (slash != std::string_view::npos && !field.has_value())
        return PropertyEditResult::Gone;
    const ComponentMeta* meta = world.GetMeta(id);
    for (const EntityId holder : holders)
    {
        if (!document.IsSceneInstanceMember(holder))
            continue;
        const std::vector<std::byte>& baseline = Baseline(holder, *serializer, id);
        const void* live = std::as_const(world).GetComponentRaw(holder, id);
        if (meta == nullptr || live == nullptr || baseline.size() != meta->Size)
            continue;
        std::vector<std::byte> current(meta->Size);
        std::memcpy(current.data(), live, meta->Size);
        std::vector<std::byte> reset = baseline;
        if (field.has_value())
        {
            const std::size_t span = field->Leaf->Size * field->Leaf->Count;
            if (field->Leaf->Offset + span > current.size())
                continue;
            reset = current;
            std::memcpy(reset.data() + field->Leaf->Offset, baseline.data() + field->Leaf->Offset, span);
        }
        if (reset != current)
            commands.push_back(std::make_unique<RawComponentEditCommand>(holder, id, std::move(current), std::move(reset),
                                                                         document.GetScene(), document));
    }
    return Run(std::move(commands));
}

void ComponentPropertySource::RestoreBefore()
{
    World& world = EditingDocument->GetScene().GetRegistry().Components;
    for (const Held& held : Holding)
        if (void* live = world.GetComponentRaw(held.Entity, EditingComponent); live != nullptr)
            std::memcpy(live, held.Before.data(), held.Before.size());
    // Reached as the pending scope's cancel too, which leaves nothing staged.
    if (Phase == EditPhase::Previewing)
        Phase = EditPhase::Armed;
}

PropertyEditResult ComponentPropertySource::CommitAsset(const Field& field, std::string_view path)
{
    EditorDocument& document = WorldDoc.FocusDocument();
    AssetSystem* assets = document.GetAssetSystem();
    const AssetRegistry* catalog = document.GetAssetCatalog();
    if (assets == nullptr)
        return PropertyEditResult::Gone;

    const RuntimeField& leaf = *field.Leaf;
    AssetRef picked{ .Type = leaf.Asset };
    if (!path.empty())
    {
        const AssetRecord* record = catalog != nullptr ? catalog->FindByPath(path) : nullptr;
        if (record == nullptr || record->Type != leaf.Asset)
            return PropertyEditResult::Refused;
        picked = AssetRef{ .Type = leaf.Asset, .Path = record->Path, .Id = record->Id };
    }

    World& world = document.GetScene().GetRegistry().Components;
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const EntityId entity : Entities())
    {
        const void* base = std::as_const(world).GetComponentRaw(entity, field.Component);
        if (base == nullptr)
            continue;
        const AssetFieldValue current =
            ReadAssetField(*assets, leaf.Asset, leaf.Arity, static_cast<const std::byte*>(base) + leaf.Offset);

        // One edit is one whole before/after value through the refcount-balanced
        // command: a handle retained and released in halves does not come back
        // from an undo.
        AssetFieldValue next = current;
        if (leaf.Arity == AssetArity::List)
        {
            if (field.Slot == next.Refs.size())
            {
                // Choosing on the empty slot a list always offers appends it;
                // choosing nothing there asks for nothing.
                if (picked.Path.empty())
                    continue;
                next.Refs.push_back(picked);
            }
            else if (field.Slot < next.Refs.size())
                next.Refs[field.Slot] = picked;
            else
                continue;
        }
        else
        {
            next.Refs.clear();
            if (!picked.Path.empty())
                next.Refs.push_back(picked);
        }
        commands.push_back(std::make_unique<AssetFieldEditCommand>(entity, field.Component, leaf.Offset, leaf.Asset,
                                                                   leaf.Arity, current, std::move(next),
                                                                   document.GetScene(), document, *assets));
    }
    return Run(std::move(commands));
}

std::vector<AssetFieldCandidate> ComponentPropertySource::AssetChoices(std::string_view key) const
{
    const std::optional<Field> field = Resolve(key);
    EditorDocument& document = WorldDoc.FocusDocument();
    AssetSystem* assets = document.GetAssetSystem();
    const AssetRegistry* catalog = document.GetAssetCatalog();
    if (!field.has_value() || assets == nullptr || catalog == nullptr)
        return {};
    return FindAssetFieldCandidates(*catalog, *assets, *field->Leaf);
}

std::vector<std::string> ComponentPropertySource::Candidates(std::string_view key) const
{
    std::vector<std::string> paths;
    for (AssetFieldCandidate& candidate : AssetChoices(key))
        paths.push_back(std::move(candidate.Path));
    return paths;
}
