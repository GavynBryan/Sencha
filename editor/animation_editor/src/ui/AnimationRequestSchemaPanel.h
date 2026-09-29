#pragma once

#include "ui/IEditorPanel.h"

#include <string>

class DataDocumentSet;
class DocumentSourceSet;

class AnimationRequestSchemaPanel final : public IEditorPanel
{
public:
    AnimationRequestSchemaPanel(DataDocumentSet& documents, DocumentSourceSet& sources)
        : Documents(documents)
        , Sources(sources)
    {
    }
    std::string_view GetTitle() const override { return "Request schema authoring"; }
    PanelPersistence GetPersistence() const override { return {"animation.request_schema"}; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override;
private:
    DataDocumentSet& Documents;
    DocumentSourceSet& Sources;
    std::string Message;
};
