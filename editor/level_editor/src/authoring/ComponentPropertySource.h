#pragma once

#include "authoring/AssetFieldCandidates.h"
#include "properties/PropertySource.h"

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CommandStack;
class EditorComponentAdapterRegistry;
class SelectionService;
class WorldDocument;
struct IComponentSerializer;
struct RuntimeField;

// A component whose adapter authors its own presentation, rather than its
// schema's generic fields.
inline constexpr std::string_view kComponentAdapterHint = "component-adapter";

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
// "<component>/<field name>#<slot>". Keys, not positions, so an edit still
// lands where it was aimed after the list around it changed.
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

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

    // What an asset property may be set to: stable, sorted, narrowed to the
    // data subtype the field accepts. Scans the project, so ask when a picker
    // opens rather than every frame.
    [[nodiscard]] std::vector<AssetFieldCandidate> AssetChoices(std::string_view key) const;

private:
    struct Field
    {
        const IComponentSerializer* Serializer = nullptr;
        ComponentId Component = InvalidComponentId;
        const RuntimeField* Leaf = nullptr;
        std::size_t Slot = 0;
    };
    [[nodiscard]] std::optional<Field> Resolve(std::string_view key) const;
    [[nodiscard]] PropertyEditResult CommitAsset(const Field& field, std::string_view path);
    void RestoreBefore();

    // The one byte edit open, if any: whose component it is, and the bytes it
    // had when the edit began.
    enum class EditPhase : std::uint8_t { Idle, Armed, Previewing };

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    const EditorComponentAdapterRegistry& Adapters;

    EditPhase Phase = EditPhase::Idle;
    std::string EditingKey;
    EntityId EditingEntity{};
    ComponentId EditingComponent = InvalidComponentId;
    std::vector<std::byte> Before;
};
