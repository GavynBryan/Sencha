#pragma once

#include "DataEditorWorkspace.h"
#include "ui/DataSubtypeEditorRegistry.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <string>

class Engine;
struct ProjectDescriptor;

// The data workspace: every structured data asset in the project, edited
// through the schema form and the purpose-built surfaces its subtypes register.
class DataWorkspace final : public IWorkspace
{
public:
    DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                  DataDocumentStore& store);
    ~DataWorkspace() override;

    DataWorkspace(const DataWorkspace&) = delete;
    DataWorkspace& operator=(const DataWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override { Visible = visible; }
    [[nodiscard]] bool ClaimPlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    WorkspaceView& View() override { return Surface; }

private:
    void BuildUi();
    void RegisterCommands();
    void SaveActive();

    Engine& EngineRef;
    DataEditorWorkspace Model;
    // The purpose-built authoring surfaces; their panels never outlive them.
    DataSubtypeEditorRegistry SubtypeEditors;
    bool Visible = false;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
