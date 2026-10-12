#pragma once

#include "problems/ProblemList.h"

#include <ui/UiAction.h>
#include <ui/UiDiagnostic.h>
#include <ui/UiSurface.h>

#include <deque>
#include <string>
#include <string_view>
#include <vector>

struct UiPreviewModel;

// Where a problem with the preview model's declarations is located: the model,
// not a line of the document.
inline constexpr std::string_view kPreviewModelLocation = "preview model";

// The warnings and errors among a session's diagnostics that concern the
// previewed document's surface, each listed once however often it was said.
[[nodiscard]] std::vector<Problem> PreviewProblems(const std::deque<UiDiagnostic>& diagnostics, UiSurfaceId surface);

// What a raised action said, by its declared name: `play (int 3)`.
[[nodiscard]] std::string DescribeRaisedAction(const UiAction& action, const UiPreviewModel& model);
