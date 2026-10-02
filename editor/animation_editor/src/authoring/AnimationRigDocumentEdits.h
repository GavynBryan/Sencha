#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct AnimMaskOp;
class AnimationPreviewSession;
class DataAssetCache;
class DataDocumentSet;

// One undo step on the rig document, opened if it is not; `edit` returns whether it changed anything.
bool EditAnimationRig(DataDocumentSet& documents, const std::string& rigPath,
                      const std::function<bool(JsonValue&)>& edit, std::string& error);

// A layer's mask steps as authored: from the open rig document, which may be
// ahead of the bound rig, else from the loaded rig.
[[nodiscard]] std::vector<AnimMaskOp> AnimationMaskSteps(DataDocumentSet& documents, const DataAssetCache& data,
                                                         const std::string& rigPath, std::size_t layer);

// One undo step; each of the verb's arguments is fed by an input of its own name.
bool CreateAnimationBinding(DataDocumentSet& documents, const AnimationPreviewSession& simulation,
                            const std::string& bindingsPath, const std::string& key, const std::string& verb,
                            std::string& error);

// Gameplay tags the simulated rig's content uses that nothing declares.
[[nodiscard]] std::vector<std::string> UndeclaredAnimationNamesOf(const AnimationPreviewSession& simulation,
                                                                  const DataDocumentSet& documents);

// One undo step on the tag declarations beside the rig, created if absent.
bool DeclareUndeclaredAnimationNames(DataDocumentSet& documents, const AnimationPreviewSession& simulation,
                                     const std::string& rigPath, std::string& error);
