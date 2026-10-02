#include "authoring/AnimationTraceProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <format>

namespace
{
// Shown at once; a filter narrows the rest.
constexpr std::size_t kRowsShown = 200;
} // namespace

std::vector<PropertyNode> AnimationTraceProperties::Nodes() const
{
    PropertyNode file = TextProperty("file", "Trace file", File, "A file exported with anim.trace.export");
    file.Descriptor.Verbs = { { .Id = "read", .Label = "Read Trace" } };
    std::vector<PropertyNode> nodes{ std::move(file) };
    if (!Error.empty())
        nodes.push_back(NoteProperty("error", "Problem", Error));
    if (!Trace)
        return nodes;

    PropertyNode whose = NoteProperty("whose", "Entity", std::format("{}, rig {}", Trace->Entity,
                                                                     Trace->Rig.empty() ? "(unbound)" : Trace->Rig));
    if (!Trace->Rig.empty())
        whose.Descriptor.Verbs = { { .Id = "open", .Label = "Open Its Rig" } };
    nodes.push_back(std::move(whose));
    nodes.push_back(NoteProperty("captured", "Captured", Trace->Captured.empty() ? "unknown" : Trace->Captured));
    if (Trace->Overwritten() > 0)
        nodes.push_back(NoteProperty("overwritten", "Lost",
                                     std::format("The {} earliest records were overwritten before export.",
                                                 Trace->Overwritten())));
    nodes.push_back(TextProperty("filter", "Filter", Filter, "By cause, layer or detail"));

    PropertyNode rows = GroupProperty("rows", "Records");
    std::size_t matched = 0;
    for (const AnimationTraceRow& row : Trace->Rows)
    {
        if (!Filter.empty() && row.Cause.find(Filter) == std::string::npos
            && row.Layer.find(Filter) == std::string::npos && row.Detail.find(Filter) == std::string::npos)
            continue;
        if (matched++ < kRowsShown)
            rows.Children.push_back(NoteProperty(
                std::format("rows.{}", rows.Children.size()),
                std::format("{} {}{}", row.Tick, row.Cause, row.Layer.empty() ? "" : " on " + row.Layer), row.Detail));
    }
    rows.Descriptor.Label = matched > kRowsShown ? std::format("Records, the first {} of {}", kRowsShown, matched)
                                                 : std::format("Records, {}", matched);
    nodes.push_back(std::move(rows));
    return nodes;
}

PropertyEditResult AnimationTraceProperties::CommitEdit(std::string_view key, std::string_view value)
{
    if (key == "file")
        File = value;
    else if (key == "filter")
        Filter = value;
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationTraceProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key == "file" && verb == "read")
    {
        Error.clear();
        Trace = ReadAnimationTraceFile(File, Error);
        return Trace ? PropertyEditResult::Applied : PropertyEditResult::Refused;
    }
    if (key == "whose" && verb == "open" && Trace)
    {
        if (Workspace.OpenRig(Trace->Rig))
            return PropertyEditResult::Applied;
        Error = Workspace.Rig.Error;
        return PropertyEditResult::Refused;
    }
    return PropertyEditResult::Gone;
}
