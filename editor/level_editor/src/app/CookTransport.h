#pragma once

#include "actions/ActionFamily.h"
#include "workspaces/facets/TransportFacet.h"

#include <cstdint>
#include <functional>

class ActionCatalog;
class ActionContext;
class CookProfilesModal;
class EditorCookRuntime;

// Cooking and playing as actions and as the rail's readout. A cook settles
// open previews first, or it would read them half-staged; each cook profile is
// an action of its own, following the profiles' edits.
class CookTransport
{
public:
    CookTransport(EditorCookRuntime& cook, CookProfilesModal* profiles, std::function<void()> settle);

    void Offer(ActionCatalog& catalog, ActionContext& context);
    // Per frame, before the UI updates: the profiles dialog, and its edits
    // reaching the profile actions.
    void Update(ActionCatalog& catalog, ActionContext& context);
    [[nodiscard]] WorkspaceTransportFacet Facet() const;

private:
    void OfferProfiles(ActionCatalog& catalog, ActionContext& context);

    EditorCookRuntime& Cook;
    CookProfilesModal* Profiles = nullptr;
    std::function<void()> Settle;
    ActionFamily ProfileActions;
    std::uint64_t OfferedRevision = 0;
};
