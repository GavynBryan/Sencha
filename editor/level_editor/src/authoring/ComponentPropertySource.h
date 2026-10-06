#pragma once

#include "authoring/AssetFieldCandidates.h"
#include "properties/PropertySource.h"

#include <ecs/ComponentId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CommandStack;
class EditorComponentAdapterRegistry;
class SelectionService;
class WorldDocument;
struct EditorComponentInspectorContext;
struct IComponentSerializer;
struct RuntimeField;

//=============================================================================
// ComponentPropertySource
//
// The primary selected entity's components, as properties. Registry-driven: it
// walks the component serializer registry and presents each present
// component's schema leaves, so any component -- engine or game module -- is
// shown and edited without this naming its type.
//
// A component is a Group keyed by its serializer's JSON key; a field is keyed
// "<component>/<field name>", and one slot of a list of assets
// "<component>/<field name>#<slot>". A component whose adapter authors its
// presentation shows the adapter's properties instead, keyed
// "<component>:<adapter key>". Keys, not positions, so an edit still lands
// where it was aimed after the list around it changed.
//
// For a member of a scene instance, a field that differs from the
// placement's source carries a "reset" verb, and so does its component.
//
// Byte fields preview into the live component inside the command stack's
// pending-edit scope and commit as one RawComponentEditCommand, so undo, save
// and every interruption revert an unfinished edit. An asset field is never
// text: its value is an asset path, committed through AssetFieldEditCommand.
//=============================================================================
class ComponentPropertySource final : public IPropertySource
{
public:
    ComponentPropertySource(WorldDocument& world,
                            SelectionService& selection,
                            CommandStack& commands,
                            const EditorComponentAdapterRegistry& adapters);
    ~ComponentPropertySource() override;

    ComponentPropertySource(const ComponentPropertySource&) = delete;
    ComponentPropertySource& operator=(const ComponentPropertySource&) = delete;

    // The entity whose components these are; invalid with nothing selected.
    [[nodiscard]] EntityId Entity() const;

    // Components presented somewhere other than this list -- an identity in a
    // title -- left out of it; still edited here by key.
    void ShowElsewhere(std::vector<ComponentTypeId> types);
    // The key a field of a component is edited by, or empty if no registered
    // component has it.
    [[nodiscard]] std::string KeyOf(ComponentTypeId type, std::string_view field) const;

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
    // A component's "remove" and "reset", an overridden field's "reset",
    // "add" on "components" with the component to add, or an adapter's verb.
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // What an asset property may be set to: stable, sorted, narrowed to the
    // data subtype the field accepts. Scans the project, so ask when a picker
    // opens rather than every frame.
    [[nodiscard]] std::vector<AssetFieldCandidate> AssetChoices(std::string_view key) const;
    // The same, as the paths an edit takes.
    [[nodiscard]] std::vector<std::string> Candidates(std::string_view key) const override;

private:
    struct Field
    {
        const IComponentSerializer* Serializer = nullptr;
        ComponentId Component = InvalidComponentId;
        const RuntimeField* Leaf = nullptr;
        std::size_t Slot = 0;
    };
    [[nodiscard]] std::optional<Field> Resolve(std::string_view key) const;
    [[nodiscard]] EditorComponentInspectorContext InspectorContext(EntityId entity) const;
    // The component as the placement's source defines it, for a member of a
    // scene instance: empty when the source has no such component. Built on a
    // scratch entity, so kept until the selection moves to another entity.
    [[nodiscard]] const std::vector<std::byte>& Baseline(EntityId entity, IComponentSerializer& serializer,
                                                         ComponentId component) const;
    [[nodiscard]] PropertyEditResult ActOnComponent(EntityId entity, std::string_view key, std::string_view verb,
                                                    std::string_view choice);
    [[nodiscard]] PropertyEditResult CommitAsset(const Field& field, std::string_view path);
    void RestoreBefore();

    // The one byte edit open, if any: whose component it is, and the bytes it
    // had when the edit began.
    enum class EditPhase : std::uint8_t { Idle, Armed, Previewing };

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    const EditorComponentAdapterRegistry& Adapters;

    std::vector<ComponentTypeId> ShownElsewhere;

    EditPhase Phase = EditPhase::Idle;
    std::string EditingKey;
    EntityId EditingEntity{};
    ComponentId EditingComponent = InvalidComponentId;
    std::vector<std::byte> Before;

    mutable EntityId BaselineEntity{};
    mutable std::unordered_map<ComponentId, std::vector<std::byte>> Baselines;
};
