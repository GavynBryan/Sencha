#pragma once

#include "problems/ProblemList.h"
#include "properties/PropertySource.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

class ActionCatalog;
class ActionContext;
class CommandStack;
class EditorWorkspace;
struct ShadowResidencyReadout;

// The views' baked-lighting preview: the last cook's lightmaps drawn in the
// Solid views.
enum class BakedLightingPreview : std::uint8_t
{
    Unavailable, // nothing cooked yet this session
    Off,
    Fresh,
    Stale,       // the document changed since the cook it shows
};

struct LightingCook
{
    std::uint32_t ProbeVolumes = 0;
    std::uint32_t Probes = 0;
};

// What the renderer and the cook say about the lighting, reached through the
// workspace that owns them; any may be missing when the renderer never came up.
struct LightingReports
{
    const ShadowResidencyReadout* Shadows = nullptr;
    std::function<BakedLightingPreview()> Preview = {};
    std::function<void(bool)> ShowPreview = {};
    std::function<void()> RerenderShadows = {};
    std::function<std::optional<LightingCook>()> LastCook = {};
};

//=============================================================================
// LightingDiagnostics
//
// How the lighting is doing, said where it applies: a selected light's shadow
// (its tier, update policy, slot and state, and how full its pool is), what a
// baked light or a probe volume became at the last cook, lights the shadow
// budget left unshadowed and a stale baked preview as problems, and the
// preview and a shadow re-render as view actions. Read-only: nothing here edits
// the document.
//=============================================================================
class LightingDiagnostics final : public ReadOnlyPropertySource
{
public:
    LightingDiagnostics(EditorWorkspace& workspace, CommandStack& commands, LightingReports reports);

    void Offer(ActionCatalog& catalog, ActionContext& context);

    // Located by the light's persistent id ("entity:<id>").
    [[nodiscard]] std::vector<Problem> Problems(const DocumentRef& document) const;
    // Selects the light a problem names; false when it names nothing here.
    bool Reveal(std::string_view location);

    // The selected entity's lighting facts; empty when it has none.
    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    [[nodiscard]] EntityId SelectedEntity() const;
    [[nodiscard]] std::vector<PropertyNode> ShadowNodes(EntityId entity) const;

    EditorWorkspace& Workspace;
    CommandStack& Commands;
    LightingReports Reports;
};
