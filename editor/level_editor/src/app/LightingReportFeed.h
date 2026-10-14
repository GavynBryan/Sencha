#pragma once

#include "workspace/LightingDiagnostics.h"

class EditorCookRuntime;
class EditorRenderFeature;

// What the lighting diagnostics read and do, from the view renderer (the
// shadow readout, the baked preview it draws) and the cook (its last record).
// Without a renderer the preview reads as unavailable; both must outlive the
// reports.
[[nodiscard]] LightingReports LightingReportsFrom(EditorRenderFeature* feature, EditorCookRuntime& cook);
