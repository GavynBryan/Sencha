#pragma once

#include "problems/ProblemList.h"

#include <anim/AnimDiagnostic.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

class DataDocumentStore;

// Where the undeclared-names problem points: the stage card, which declares them.
inline constexpr std::string_view kUndeclaredAnimationNames = "undeclared names";

// The rig's and the scenario's diagnostics as the editor's problems, each in
// the data document it was found in when it was found in one, and the names
// the content uses that nothing declares as one problem more.
[[nodiscard]] std::vector<Problem> AnimationRigProblems(std::span<const AnimDiagnostic> diagnostics,
                                                        std::span<const std::string> undeclared,
                                                        DataDocumentStore& store);
