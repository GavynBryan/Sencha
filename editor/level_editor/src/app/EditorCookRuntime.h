#pragma once

#include "project/CookSession.h"
#include "project/PieDriver.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ConsoleRegistry;
class CookProfilesPanel;
class Engine;
class SceneRenderQueueBuilder;
class WorldDocument;
struct ProjectDescriptor;
struct RuntimeAssets;

// Cooking and the player launched from it. They are one group because the player
// consumes what the cook publishes: the serials exist so a freshly published
// cook is handed to the driver exactly once, and so an enabled lightmap preview
// refreshes against the newest bake. Nothing outside reconciles those.
class EditorCookRuntime
{
public:
    EditorCookRuntime(Engine& engine, WorldDocument& world, ProjectDescriptor* project, RuntimeAssets* assets);

    void RegisterConsoleCommands(ConsoleRegistry& registry);

    // Republishes a completed cook into the player, and refreshes the baked
    // lighting preview when one is showing. Called once per frame; each step is
    // a no-op until its serial moves.
    void Update(SceneRenderQueueBuilder* previewBuilder);

    // Loads the newest cook into the preview immediately, for the moment the
    // user switches the preview on rather than waiting for the next cook.
    void RefreshPreviewNow(SceneRenderQueueBuilder& previewBuilder);

    // Starts the selected profile. `rebuild` forces a full cook rather than
    // reusing cached steps.
    void Start(bool rebuild = false);
    void Cancel();
    [[nodiscard]] bool IsActive() const { return Session.IsActive(); }

    [[nodiscard]] const CookSession::Record* LastRecord() const { return Session.LastRecord(); }
    [[nodiscard]] CookSession& GetSession() { return Session; }
    [[nodiscard]] PieDriver& Player() { return Driver; }

    [[nodiscard]] const std::string& SelectedProfileId() const { return SelectedProfile; }
    void SelectProfile(std::string id) { SelectedProfile = std::move(id); }

    // The panel listing profiles, so the toolbar's profile menu and the panel
    // agree on the selection. Non-owning; the UI feature owns the panel.
    void SetProfilesPanel(CookProfilesPanel* panel) { Profiles = panel; }
    [[nodiscard]] CookProfilesPanel* ProfilesPanel() const { return Profiles; }

private:
    CookSession Session;
    PieDriver   Driver;

    CookProfilesPanel* Profiles = nullptr;
    std::string        SelectedProfile = "full";
    // The publication the player was last pointed at, and the cook the lightmap
    // preview was last refreshed from. Both guard against redoing the handoff.
    std::uint64_t      AppliedSerial = 0;
    std::uint64_t      PreviewSerial = 0;
};
