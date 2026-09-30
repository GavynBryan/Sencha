#pragma once

#include <functional>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;

//=============================================================================
// EntityVerbs
//
// What can be done to the selected entities as parts of the scene rather than
// as geometry, as actions in the Entity category: a new entity, parenting
// under the primary and clearing a parent, hiding and locking, a placement's
// source opened or broken, the scene's origin moved to the primary, and one
// "Move to <zone>" per zone the selection could move to, kept in step with the
// world's zones.
//=============================================================================
class EntityVerbs
{
public:
    // `openSource` opens the scene source an asset path names; false when it
    // cannot be found.
    EntityVerbs(EditorWorkspace& workspace, std::function<bool(const std::string&)> openSource);

    void Offer(ActionCatalog& catalog, ActionContext& context);
    // Per frame: offers a move for every zone the world has now and withdraws
    // the moves to zones that went.
    void OfferZoneMoves(ActionCatalog& catalog, ActionContext& context);

    // The verbs for what is selected, in the order a person meets them.
    [[nodiscard]] std::vector<std::string> Relevant() const;

private:
    EditorWorkspace& Workspace;
    std::function<bool(const std::string&)> OpenSource;
    // The zone moves in play, and the zone names they were offered under.
    std::vector<std::string> OfferedMoves;
    std::vector<std::string> OfferedNames;
};
