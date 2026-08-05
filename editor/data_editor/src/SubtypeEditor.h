#pragma once

#include "DataFormEdit.h"
#include "ui/IEditorPanel.h"

#include <memory>
#include <string_view>
#include <vector>

union SDL_Event;

struct DataSchema;
class DataDocument;
class DataEditorWorkspace;

// Everything a purpose-built form needs for one draw of the active document.
//
// Data is the working copy's "data" object, mutated in place. Edits are
// reported through the returned FieldEdit and landed by the caller, so an
// editor never drives the document's transaction itself and every authoring
// surface coalesces undo the same way.
struct SubtypeFormContext
{
    JsonValue& Data;
    const DataSchema& Schema;
    const DataDocument& Document;
    DataEditorWorkspace& Workspace;
};

// A purpose-built authoring surface for one data subtype.
//
// The schema-generated form can express any subtype, but not always well: a
// list of bindings reads as JSON with buttons when what the author wants is a
// keymap. An editor claims one subtype and draws it properly, and the data
// editor's core learns only this interface -- never which subtypes exist.
//
// An editor owns whatever state it needs (compiled previews, capture) and is
// responsible for keying that state to the document it is shown, since only the
// active document's editor is driven.
class IDataSubtypeEditor
{
public:
    virtual ~IDataSubtypeEditor() = default;

    IDataSubtypeEditor(const IDataSubtypeEditor&) = delete;
    IDataSubtypeEditor& operator=(const IDataSubtypeEditor&) = delete;

    // The document `type` value this editor claims. Spelled by the feature that
    // owns the subtype, never here.
    [[nodiscard]] virtual std::string_view Subtype() const = 0;

    // Draw the whole document. Delegate any subtree with nothing better to
    // offer back to DrawDataField, so nothing becomes unauthorable just because
    // a specialized surface exists for its siblings.
    [[nodiscard]] virtual FieldEdit DrawForm(SubtypeFormContext& ctx) = 0;

    // Once per frame while a document of this subtype is active, whether or not
    // the panels reading the result are visible.
    virtual void UpdateForFrame(const DataDocument& document,
                                DataEditorWorkspace& workspace)
    {
        (void)document;
        (void)workspace;
    }

    // Raw platform events while a document of this subtype is active, offered
    // before the UI layer sees them. Return true to consume the event, which
    // stops it reaching both ImGui and the editor's shortcuts.
    virtual bool HandlePlatformEvent(const SDL_Event& event)
    {
        (void)event;
        return false;
    }

    // Panels this editor contributes, created once when the UI is built. They
    // typically hold references into the editor's own state, which outlives
    // them by construction: the registry owning the editors is destroyed after
    // the renderer feature owning the panels.
    [[nodiscard]] virtual std::vector<std::unique_ptr<IEditorPanel>>
    CreatePanels(DataEditorWorkspace& workspace)
    {
        (void)workspace;
        return {};
    }

protected:
    IDataSubtypeEditor() = default;
};
