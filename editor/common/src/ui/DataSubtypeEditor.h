#pragma once

#include "ui/DataForm.h"
#include "ui/IEditorPanel.h"

#include <memory>
#include <string_view>
#include <vector>

union SDL_Event;

struct DataSchema;
class DataDocument;
class DataDocumentSet;

// Data is the working copy's "data" object, mutated in place; the edit is
// returned and landed by the caller, so every surface coalesces undo alike.
struct DataSubtypeFormContext
{
    JsonValue& Data;
    const DataSchema& Schema;
    const DataDocument& Document;
    DataDocumentSet& Documents;
};

// A purpose-built authoring surface for one data subtype. Only the active
// document's editor is driven, so an editor keys its own state to the document.
class IDataSubtypeEditor
{
public:
    virtual ~IDataSubtypeEditor() = default;

    IDataSubtypeEditor(const IDataSubtypeEditor&) = delete;
    IDataSubtypeEditor& operator=(const IDataSubtypeEditor&) = delete;

    [[nodiscard]] virtual std::string_view Subtype() const = 0;

    // Delegate any subtree with nothing better to offer back to DrawDataField.
    [[nodiscard]] virtual FieldEdit DrawForm(DataSubtypeFormContext& ctx) = 0;

    // Every frame a document of this subtype is active, visible panels or not.
    virtual void UpdateForFrame(const DataDocument& document, DataDocumentSet& documents)
    {
        (void)document;
        (void)documents;
    }

    // Offered before the UI layer; true consumes the event.
    virtual bool HandlePlatformEvent(const SDL_Event& event)
    {
        (void)event;
        return false;
    }

    // Created once; the registry owning the editors outlives the panels.
    [[nodiscard]] virtual std::vector<std::unique_ptr<IEditorPanel>> CreatePanels(DataDocumentSet& documents)
    {
        (void)documents;
        return {};
    }

protected:
    IDataSubtypeEditor() = default;
};
