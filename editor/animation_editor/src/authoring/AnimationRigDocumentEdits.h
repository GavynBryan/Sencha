#pragma once

#include <core/json/JsonValue.h>

#include <functional>
#include <string>
#include <vector>

class AnimationPreviewSession;
class DataDocumentSet;

// One undo step on the rig document, opened if it is not; `edit` returns whether it changed anything.
bool EditAnimationRig(DataDocumentSet& documents, const std::string& rigPath,
                      const std::function<bool(JsonValue&)>& edit, std::string& error);

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
