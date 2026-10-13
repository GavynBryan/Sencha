#pragma once

#include "actions/ContextMenuModel.h"
#include "workspaces/facets/ResourceFacet.h"

#include <core/assets/AssetRef.h>

#include <vector>

class AssetPictures;
class EditorWorkspace;

// What the Level's hands hold: the kind of asset the tool in hand consumes --
// a material, unless the tool says otherwise -- with the one in use and those
// put by, pictured.
[[nodiscard]] AssetType HeldType(const EditorWorkspace& workspace);
[[nodiscard]] WorkspaceResourceFacet HeldResourceFacet(const EditorWorkspace& workspace, AssetPictures& pictures);
// The assets of `type` to hand: the one in use, then the ones put by.
[[nodiscard]] std::vector<ContextMenuModel::AssetChoice> AssetsToHand(const EditorWorkspace& workspace, AssetType type,
                                                                      AssetPictures& pictures);
