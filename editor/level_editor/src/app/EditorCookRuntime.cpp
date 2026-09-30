#include "EditorCookRuntime.h"

#include "render/SceneRenderQueueBuilder.h"

#include <core/console/ConsoleRegistry.h>

EditorCookRuntime::EditorCookRuntime(Engine& engine, WorldDocument& world, ProjectDescriptor* project,
                                     RuntimeAssets* assets)
    : Session(engine, world, project, assets)
    , Driver(engine, world, project, assets)
{
}

void EditorCookRuntime::RegisterConsoleCommands(ConsoleRegistry& registry)
{
    Session.RegisterCommands(registry);
    Driver.RegisterCommands(registry);
}

void EditorCookRuntime::Start(bool rebuild)
{
    (void)Session.StartById(SelectedProfile, rebuild);
}

void EditorCookRuntime::Cancel()
{
    Session.Cancel();
}

void EditorCookRuntime::RefreshPreviewNow(SceneRenderQueueBuilder& previewBuilder)
{
    const CookSession::Record* record = Session.LastRecord();
    if (record == nullptr)
        return;
    previewBuilder.SetLightmapPreview({ record->CookedScenePath, record->ContentHash });
    PreviewSerial = record->Serial;
}

void EditorCookRuntime::Update(SceneRenderQueueBuilder* previewBuilder)
{
    if (Session.PublicationSerial() != AppliedSerial)
    {
        AppliedSerial = Session.PublicationSerial();
        if (!Session.LastCookedWorld().empty())
            Driver.UseCookedWorld(Session.LastCookedWorld(), Session.LastCookedZone());
        else if (const CookSession::Record* record = Session.LastRecord())
            Driver.UseCookedLevel(record->Map);
    }

    // A newer cook refreshes an enabled baked-lighting preview in place.
    if (previewBuilder == nullptr || !previewBuilder->LightmapPreviewEnabled())
        return;
    const CookSession::Record* record = Session.LastRecord();
    if (record == nullptr || record->Serial == PreviewSerial)
        return;
    previewBuilder->SetLightmapPreview({ record->CookedScenePath, record->ContentHash });
    PreviewSerial = record->Serial;
}
