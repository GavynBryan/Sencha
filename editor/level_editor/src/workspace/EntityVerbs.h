#pragma once

#include "actions/ActionFamily.h"

#include <functional>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;

//=============================================================================
// EntityVerbs
// The selected entities' scene verbs as Entity actions: create, parent, hide,
// lock, open or break a placement's source, move the origin, and one
// "Move to <zone>" per zone, kept in step with the world's zones.
//=============================================================================
class EntityVerbs
{
public:
    // `openSource` opens the scene source an asset path names; false when it
    // cannot be found.
    EntityVerbs(EditorWorkspace& workspace, std::function<bool(const std::string&)> openSource,
                 std::function<void(const std::string&)> reportProblem = {});

    void Offer(ActionCatalog& catalog, ActionContext& context);
    // Per frame: offers a move for every zone the world has now and withdraws
    // the moves to zones that went.
    void OfferZoneMoves(ActionCatalog& catalog, ActionContext& context);

    // The verbs for what is selected, in the order a person meets them.
    [[nodiscard]] std::vector<std::string> Relevant() const;

private:
    EditorWorkspace& Workspace;
    std::function<bool(const std::string&)> OpenSource;
    std::function<void(const std::string&)> ReportProblem;
    ActionFamily ZoneMoves;
};
