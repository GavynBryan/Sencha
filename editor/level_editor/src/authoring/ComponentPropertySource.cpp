#include "authoring/ComponentPropertySource.h"

#include "authoring/EditorComponentAdapter.h"
#include "authoring/RuntimeFieldText.h"
#include "commands/CommandStack.h"
#include "document/AssetFieldIo.h"
#include "document/DerivedComponents.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "document/commands/AssetFieldEditCommand.h"
#include "document/commands/RawComponentAddCommand.h"
#include "document/commands/RawComponentEditCommand.h"
#include "document/commands/RawComponentRemoveCommand.h"
#include "selection/SelectionService.h"

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <memory>
#include <utility>

namespace
{
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

std::vector<PropertyNode> ComponentPropertySource::Nodes() const
{
    std::vector<PropertyNode> nodes;
    const EntityId entity = Entity();
    if (!entity.IsValid())
        return nodes;

    EditorDocument& document = WorldDoc.FocusDocument();
    const World& world = std::as_const(document.GetScene().GetRegistry()).Components;
    AssetSystem* assets = document.GetAssetSystem();

    const bool member = document.IsSceneInstanceMember(entity);
    std::vector<std::string> addable;
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        if (id == InvalidComponentId)
            continue;
        const std::string key(serializer->JsonKey());
        if (!world.HasComponent(entity, id))
        {
            // A component the document owns (one that may not be removed) is
            // not the editor's to add either: its managed state would never be
            // minted.
            if (serializer->IsRemovable())
                addable.push_back(HumanizeSchemaName(key));
            continue;
        }

        PropertyNode group{ .Key = key,
                            .Descriptor = { .Kind = PropertyKind::Group, .Label = HumanizeSchemaName(key) } };
        if (serializer->IsRemovable())
            group.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove Component", .Removes = true });

        const IEditorComponentAdapter* adapter = Adapters.Find(serializer->TypeId());
        if (adapter != nullptr)
            group.Descriptor.Actions.assign(adapter->Actions().begin(), adapter->Actions().end());
        if (adapter != nullptr && adapter->AuthorsInspectorRows())
        {
            group.Children = adapter->InspectorNodes(InspectorContext(entity));
            PrefixKeys(group.Children, key + kAdapterSeparator);
            nodes.push_back(std::move(group));
            continue;
        }

        const std::span<const RuntimeField> fields = serializer->RuntimeFields();
        // A zero-size tag has no storage, so bytes are asked for only when there
        // is a leaf to read; the group still appears, fields or not.
        const void* bytes = fields.empty() ? nullptr : world.GetComponentRaw(entity, id);
        if (!fields.empty() && bytes == nullptr)
            continue;
        const std::vector<std::byte>* baseline = member ? &Baseline(entity, *serializer, id) : nullptr;
        if (baseline != nullptr && baseline->empty())
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
                                .ReadOnly = field.ReadOnly || !IsRuntimeFieldEditable(field) },
            };
            for (const EnumOption& option : field.Enum)
                node.Descriptor.Choices.emplace_back(option.Name);
            holdsAssets |= field.Asset != AssetType::Unknown;

            if (field.Asset == AssetType::Unknown)
            {
                if (node.Descriptor.Kind != PropertyKind::Unsupported)
                    node.Value = FormatRuntimeField(field, bytes);
                if (baseline != nullptr && Overridden(field, static_cast<const std::byte*>(bytes), *baseline))
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
            const AssetFieldValue value = ReadAssetField(
                *assets, field.Asset, field.Arity, static_cast<const std::byte*>(bytes) + field.Offset);
            if (field.Arity != AssetArity::List)
            {
                node.Value = value.Refs.empty() ? std::string{} : value.Refs.front().Path;
                group.Children.push_back(std::move(node));
                continue;
            }
            // A list with no slots still offers one, so an empty field can be filled.
            const std::size_t slots = std::max<std::size_t>(value.Refs.size(), 1);
            for (std::size_t slot = 0; slot < slots; ++slot)
            {
                PropertyNode slotNode = node;
                slotNode.Key += "#" + std::to_string(slot);
                if (!value.Refs.empty())
                    slotNode.Descriptor.Label += " " + std::to_string(slot);
                slotNode.Value = slot < value.Refs.size() ? value.Refs[slot].Path : std::string{};
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
        components.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add Component", .Choices = std::move(addable) });
    nodes.insert(nodes.begin(), std::move(components));

    // What the engine put on the entity because of what was authored: always
    // there to read, closed until opened, under what each came from.
    const std::vector<DerivedComponentRow> derived = DerivedComponentsOn(world, EditorSceneSerializers(), entity);
    if (!derived.empty())
    {
        PropertyNode group{ .Key = "derived",
                            .Descriptor = { .Kind = PropertyKind::Group,
                                            .Label = std::format("Derived \xC2\xB7 {}", derived.size()),
                                            .Tooltip = "On the entity, not in the scene file: systems keep these, "
                                                       "and each goes when what it came from does.",
                                            .Folded = true } };
        for (const DerivedComponentRow& row : derived)
        {
            const ComponentMeta* owner = row.ProvidedBy == InvalidComponentId ? nullptr : world.GetMeta(row.ProvidedBy);
            const std::string source = owner != nullptr ? HumanizeSchemaName(owner->Name) : std::string("the engine");
            const std::string sourceKey = "derived/" + source;
            auto from = std::ranges::find(group.Children, sourceKey, &PropertyNode::Key);
            if (from == group.Children.end())
                from = group.Children.insert(group.Children.end(),
                                             PropertyNode{ .Key = sourceKey,
                                                           .Descriptor = { .Kind = PropertyKind::Group,
                                                                           .Label = "from " + source } });
            from->Children.push_back(PropertyNode{
                .Key = "derived/" + std::string(row.Name),
                .Descriptor = { .Kind = PropertyKind::Text, .Label = HumanizeSchemaName(row.Name),
                                .Tooltip = std::string(row.Name), .ReadOnly = true } });
        }
        nodes.push_back(std::move(group));
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

const std::vector<std::byte>& ComponentPropertySource::Baseline(EntityId entity, IComponentSerializer& serializer,
                                                                ComponentId component) const
{
    if (BaselineEntity != entity)
    {
        Baselines.clear();
        BaselineEntity = entity;
    }
    auto found = Baselines.find(component);
    if (found == Baselines.end())
        found = Baselines.emplace(component, WorldDoc.FocusDocument().BaselineComponentBytes(entity, serializer)).first;
    return found->second;
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
                return Field{ serializer.get(), id, &field, slot };
        return std::nullopt;
    }
    return std::nullopt;
}

bool ComponentPropertySource::BeginEdit(std::string_view key)
{
    // An adapter's property is set in one command when it commits; there are
    // no bytes to hold open.
    if (key.find(kAdapterSeparator) != std::string_view::npos)
        return Entity().IsValid();
    if (Phase != EditPhase::Idle)
        CancelEdit(EditingKey);
    const std::optional<Field> field = Resolve(key);
    const EntityId entity = Entity();
    if (!field.has_value() || !entity.IsValid())
        return false;
    const World& world = std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components;
    const ComponentMeta* meta = world.GetMeta(field->Component);
    const void* live = world.GetComponentRaw(entity, field->Component);
    if (meta == nullptr || live == nullptr || meta->Size == 0)
        return false;

    Before.resize(meta->Size);
    std::memcpy(Before.data(), live, meta->Size);
    EditingKey = std::string(key);
    EditingEntity = entity;
    EditingComponent = field->Component;
    Phase = EditPhase::Armed;
    return true;
}

PropertyEditResult ComponentPropertySource::PreviewEdit(std::string_view key, std::string_view value)
{
    if (key.find(kAdapterSeparator) != std::string_view::npos)
        return PropertyEditResult::Unchanged;
    if (Phase == EditPhase::Idle || key != EditingKey)
        return PropertyEditResult::Gone;
    const std::optional<Field> field = Resolve(key);
    if (!field.has_value() || field->Leaf->Asset != AssetType::Unknown)
        return PropertyEditResult::Gone;

    std::vector<std::byte> working = Before;
    if (!ParseRuntimeField(*field->Leaf, value, working.data()))
        return PropertyEditResult::Refused;

    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
    void* live = world.GetComponentRaw(EditingEntity, EditingComponent);
    if (live == nullptr)
        return PropertyEditResult::Gone;
    if (Phase == EditPhase::Armed)
    {
        // Held in the command stack's one pending-edit scope from the first
        // write on, so undo, save and any interruption put the bytes back.
        Commands.OpenPendingEdit([this] { RestoreBefore(); });
        Phase = EditPhase::Previewing;
    }
    std::memcpy(live, working.data(), working.size());
    return PropertyEditResult::Applied;
}

PropertyEditResult ComponentPropertySource::CommitEdit(std::string_view key, std::string_view value)
{
    if (const std::size_t separator = key.find(kAdapterSeparator); separator != std::string_view::npos)
    {
        const EntityId entity = Entity();
        const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, separator));
        const IEditorComponentAdapter* adapter = serializer != nullptr ? Adapters.Find(serializer->TypeId()) : nullptr;
        if (!entity.IsValid() || adapter == nullptr)
            return PropertyEditResult::Gone;
        EditorComponentInspectorContext context = InspectorContext(entity);
        return adapter->SetInspectorValue(context, key.substr(separator + 1), value);
    }
    if (Phase == EditPhase::Idle || key != EditingKey)
    {
        if (!BeginEdit(key))
            return PropertyEditResult::Gone;
    }
    const std::optional<Field> field = Resolve(key);
    if (!field.has_value())
    {
        CancelEdit(key);
        return PropertyEditResult::Gone;
    }
    if (field->Leaf->Asset != AssetType::Unknown)
    {
        CancelEdit(key);
        return CommitAsset(*field, value);
    }

    std::vector<std::byte> after = Before;
    if (!ParseRuntimeField(*field->Leaf, value, after.data()))
        return PropertyEditResult::Refused;

    std::vector<std::byte> before = Before;
    const EntityId entity = EditingEntity;
    const ComponentId component = EditingComponent;
    const bool unchanged = after == before;
    if (Phase == EditPhase::Previewing)
    {
        RestoreBefore();
        Commands.ClosePendingEdit();
    }
    Phase = EditPhase::Idle;
    EditingKey.clear();
    if (unchanged)
        return PropertyEditResult::Unchanged;

    EditorDocument& document = WorldDoc.FocusDocument();
    Commands.Execute(std::make_unique<RawComponentEditCommand>(
        entity, component, std::move(before), std::move(after), document.GetScene(), document));
    return PropertyEditResult::Applied;
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
}

PropertyEditResult ComponentPropertySource::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    const EntityId entity = Entity();
    if (!entity.IsValid())
        return PropertyEditResult::Gone;
    // A verb changes structure or bytes outright, so a byte edit left open
    // would restore over it.
    if (Phase != EditPhase::Idle)
        CancelEdit(EditingKey);
    if (const std::size_t separator = key.find(kAdapterSeparator); separator != std::string_view::npos)
    {
        const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, separator));
        const IEditorComponentAdapter* adapter = serializer != nullptr ? Adapters.Find(serializer->TypeId()) : nullptr;
        if (adapter == nullptr)
            return PropertyEditResult::Gone;
        EditorComponentInspectorContext context = InspectorContext(entity);
        return adapter->ActInspector(context, key.substr(separator + 1), verb, choice);
    }
    return ActOnComponent(entity, key, verb, choice);
}

PropertyEditResult ComponentPropertySource::ActOnComponent(EntityId entity, std::string_view key,
                                                           std::string_view verb, std::string_view choice)
{
    EditorDocument& document = WorldDoc.FocusDocument();
    World& world = document.GetScene().GetRegistry().Components;
    if (key == "components" && verb == "add")
    {
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            if (!serializer->IsRemovable() || HumanizeSchemaName(serializer->JsonKey()) != choice)
                continue;
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || world.HasComponent(entity, id))
                return PropertyEditResult::Refused;
            Commands.Execute(std::make_unique<RawComponentAddCommand>(entity, id, serializer->DefaultBytes(),
                                                                      document.GetScene(), document));
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Refused;
    }

    const std::size_t slash = key.find('/');
    IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(key.substr(0, slash));
    const ComponentId id = serializer != nullptr ? world.GetComponentIdByType(serializer->TypeId()) : InvalidComponentId;
    if (id == InvalidComponentId || !world.HasComponent(entity, id))
        return PropertyEditResult::Gone;

    if (slash == std::string_view::npos && verb == "remove")
    {
        if (!serializer->IsRemovable())
            return PropertyEditResult::Refused;
        Commands.Execute(std::make_unique<RawComponentRemoveCommand>(entity, *serializer, document.GetScene(), document));
        return PropertyEditResult::Applied;
    }
    if (verb != "reset")
        return PropertyEditResult::Gone;

    const ComponentMeta* meta = world.GetMeta(id);
    const std::vector<std::byte>& baseline = Baseline(entity, *serializer, id);
    const void* live = std::as_const(world).GetComponentRaw(entity, id);
    if (meta == nullptr || live == nullptr || baseline.size() != meta->Size)
        return PropertyEditResult::Refused;
    std::vector<std::byte> current(meta->Size);
    std::memcpy(current.data(), live, meta->Size);
    std::vector<std::byte> reset = baseline;
    if (slash != std::string_view::npos)
    {
        const std::optional<Field> field = Resolve(key);
        if (!field.has_value())
            return PropertyEditResult::Gone;
        const std::size_t span = field->Leaf->Size * field->Leaf->Count;
        if (field->Leaf->Offset + span > current.size())
            return PropertyEditResult::Refused;
        reset = current;
        std::memcpy(reset.data() + field->Leaf->Offset, baseline.data() + field->Leaf->Offset, span);
    }
    if (reset == current)
        return PropertyEditResult::Unchanged;
    Commands.Execute(std::make_unique<RawComponentEditCommand>(entity, id, std::move(current), std::move(reset),
                                                               document.GetScene(), document));
    return PropertyEditResult::Applied;
}

void ComponentPropertySource::RestoreBefore()
{
    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
    if (void* live = world.GetComponentRaw(EditingEntity, EditingComponent); live != nullptr)
        std::memcpy(live, Before.data(), Before.size());
    // Reached as the pending scope's cancel too, which leaves nothing staged.
    if (Phase == EditPhase::Previewing)
        Phase = EditPhase::Armed;
}

PropertyEditResult ComponentPropertySource::CommitAsset(const Field& field, std::string_view path)
{
    const EntityId entity = Entity();
    EditorDocument& document = WorldDoc.FocusDocument();
    AssetSystem* assets = document.GetAssetSystem();
    const AssetRegistry* catalog = document.GetAssetCatalog();
    if (!entity.IsValid() || assets == nullptr)
        return PropertyEditResult::Gone;

    World& world = document.GetScene().GetRegistry().Components;
    const void* base = std::as_const(world).GetComponentRaw(entity, field.Component);
    if (base == nullptr)
        return PropertyEditResult::Gone;
    const RuntimeField& leaf = *field.Leaf;
    const AssetFieldValue current =
        ReadAssetField(*assets, leaf.Asset, leaf.Arity, static_cast<const std::byte*>(base) + leaf.Offset);

    AssetFieldRef picked;
    if (!path.empty())
    {
        const AssetRecord* record = catalog != nullptr ? catalog->FindByPath(path) : nullptr;
        if (record == nullptr || record->Type != leaf.Asset)
            return PropertyEditResult::Refused;
        picked = AssetFieldRef{ record->Id, record->Path };
    }

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
                return PropertyEditResult::Unchanged;
            next.Refs.push_back(std::move(picked));
        }
        else if (field.Slot < next.Refs.size())
        {
            next.Refs[field.Slot] = std::move(picked);
        }
        else
        {
            return PropertyEditResult::Gone;
        }
    }
    else
    {
        next.Refs.clear();
        if (!picked.Path.empty())
            next.Refs.push_back(std::move(picked));
    }

    Commands.Execute(std::make_unique<AssetFieldEditCommand>(
        entity, field.Component, leaf.Offset, leaf.Asset, leaf.Arity, current, std::move(next),
        document.GetScene(), document, *assets));
    return PropertyEditResult::Applied;
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
