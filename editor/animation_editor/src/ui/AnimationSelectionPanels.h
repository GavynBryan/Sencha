#pragma once

class AnimationPreviewWorkspace;
class EditorUiFeature;

// Selection surfaces: the rule table with its typed predicate builder, the
// behavior inspector, the effective slot map, and the decision debugger for
// the live or a recorded tick. Selecting a rule, a behavior, a row or content
// moves the workspace's navigation and nothing else, so following a decision
// from rule to clip never disturbs the running simulation.
void AddAnimationSelectionPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace);
