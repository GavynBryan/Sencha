#include "authoring/AnimationMigrationProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <format>

void AnimationMigrationProperties::ScanOnce()
{
    if (Scanned)
        return;
    Workspace.ClipPlayers.Scan();
    Scanned = true;
}

std::vector<PropertyNode> AnimationMigrationProperties::Nodes() const
{
    const AnimationClipPlayerScan& scan = Workspace.ClipPlayers;
    PropertyNode about = NoteProperty("scan", "Clip players",
                                      "Scenes may still name a clip directly on an AnimationClipPlayer, which no longer "
                                      "exists: such an entity loads unposed. Converting gives each a one-layer rig "
                                      "that plays the clip as the player did -- same time, speed, and loop or clamp.");
    about.Descriptor.Verbs = { { .Id = "scan", .Label = "Scan Project" } };
    if (!scan.Uses.empty())
        about.Descriptor.Verbs.push_back({ .Id = "convert", .Label = "Convert to Rigs" });
    std::vector<PropertyNode> nodes{ std::move(about) };
    if (!Status.empty())
        nodes.push_back(NoteProperty("status", "Last", Status));
    for (std::size_t p = 0; p < scan.Problems.size(); ++p)
        nodes.push_back(NoteProperty(std::format("problems.{}", p), "Problem", scan.Problems[p]));
    if (scan.Uses.empty())
    {
        nodes.push_back(NoteProperty("uses.none", "Scenes", "No scene in the project names a clip player."));
        return nodes;
    }
    PropertyNode uses = GroupProperty("uses", std::format("{} uses", scan.Uses.size()));
    for (std::size_t u = 0; u < scan.Uses.size(); ++u)
    {
        const AnimationClipPlayerUse& use = scan.Uses[u];
        uses.Children.push_back(NoteProperty(std::format("uses.{}", u), std::format("{} in {}", use.Entity, use.Scene),
                                             std::format("{}, {:.2f}s at {:.2f}x, {}", use.Clip, use.TimeSeconds,
                                                         use.Rate, use.Loop ? "loop" : "clamp")));
    }
    nodes.push_back(std::move(uses));
    return nodes;
}

PropertyEditResult AnimationMigrationProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key != "scan")
        return PropertyEditResult::Gone;
    if (verb == "scan")
    {
        Workspace.ClipPlayers.Scan();
        Scanned = true;
        Status.clear();
        return PropertyEditResult::Applied;
    }
    if (verb != "convert")
        return PropertyEditResult::Gone;
    std::string error;
    const bool converted = Workspace.MigrateClipPlayers(error);
    Status = converted ? std::string("Converted.") : error;
    return converted ? PropertyEditResult::Applied : PropertyEditResult::Refused;
}
