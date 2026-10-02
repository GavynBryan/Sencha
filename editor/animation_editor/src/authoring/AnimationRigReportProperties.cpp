#include "authoring/AnimationRigReportProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRequestReport.h>
#include <anim/AnimRigRisk.h>
#include <anim/AnimWorldReport.h>

#include <format>

std::vector<PropertyNode> AnimationRigReportProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr)
        return {};
    const AnimRigRisk risk = MeasureAnimRigRisk(*rig, session.Tags());
    PropertyNode measure = GroupProperty("risk", "Content risk");
    measure.Descriptor.Tooltip = "Each count is a sign a rig is growing toward a graph; none of them is an error.";
    measure.Children = {
        NoteProperty("risk.counts", "Counts",
                     std::format("{} blend overrides, {} selector rules (the largest selector weighs {}), {} flows "
                                 "longer than {} sections",
                                 risk.BlendOverrides, risk.SelectorRules, risk.DeepestSelector, risk.LongFlows,
                                 kAnimRiskFlowSections)),
    };
    for (std::size_t f = 0; f < risk.Findings.size(); ++f)
        measure.Children.push_back(NoteProperty(std::format("risk.findings.{}", f), "Finding", risk.Findings[f].Message));
    std::vector<PropertyNode> nodes{ std::move(measure) };

    const World* world = session.SimulationWorld();
    const EntityId subject = session.Subject();
    if (world == nullptr || !subject.IsValid())
        return nodes;
    PropertyNode entity = GroupProperty("entity", "The previewed entity");
    entity.Children.push_back(
        NoteProperty("entity.bytes", "Animation state", std::format("{} bytes", AnimEntityBytes(*world, subject))));
    if (const AnimRequestReport* report = world->TryGet<AnimRequestReport>(subject))
        entity.Children.push_back(NoteProperty("entity.unplayed", "Unplayed requests",
                                               std::format("{} ended with no layer playing them", report->Unplayed)));
    nodes.push_back(std::move(entity));
    return nodes;
}
