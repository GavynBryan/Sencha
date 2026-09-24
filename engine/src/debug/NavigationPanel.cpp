#include <debug/NavigationPanel.h>

#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavTileMesh.h>
#include <navigation/NavigationSystem.h>
#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <imgui.h>

NavigationPanel::NavigationPanel(const RuntimeWorld& world, const NavigationSystem& navigation)
    : World(world)
    , Navigation(navigation)
{
}

void NavigationPanel::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Navigation"))
    {
        ImGui::End();
        return;
    }

    const NavigationSystem::LinkStateStats& stats = Navigation.LastLinkStateStats();
    ImGui::Text("link state: %zu components, %zu changed, %zu name unknown links",
                stats.StateComponents, stats.LinksChanged, stats.UnknownLinks);
    const GameplayTagRegistry* tags =
        World.Entities().TryGetResource<GameplayTagRegistry>();

    const NavigationQuery query = Navigation.Queries();
    if (Navigation.Zones().empty())
        ImGui::TextUnformatted("no resident zone has navigation");
    for (const ZoneId zone : Navigation.Zones())
    {
        const ZoneNavigation* navigation = query.FindZone(zone);
        if (navigation == nullptr)
            continue;
        ImGui::PushID(static_cast<int>(zone.Value & 0x7fffffff));
        if (ImGui::CollapsingHeader("zone", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("zone %016llx  generation %u",
                        static_cast<unsigned long long>(zone.Value), navigation->Generation());
            for (std::uint16_t p = 0; p < navigation->ProfileCount(); ++p)
            {
                const NavTileMesh& mesh = navigation->Mesh(p);
                ImGui::BulletText("%s: %zu tiles, %zu polygons",
                                  navigation->ProfileName(p).c_str(), mesh.TileCount(),
                                  mesh.PolygonCount());
            }
            for (std::uint32_t i = 0; i < navigation->LinkCount(); ++i)
            {
                const NavLinkInfo& link = navigation->Link(i);
                const std::string_view kind = tags != nullptr && link.Traversal.IsValid()
                    ? tags->GetName(link.Traversal) : std::string_view("<unbound>");
                ImGui::BulletText("link %s  %.*s  %s  x%.2f  rev %u",
                                  NavLinkIdToString(link.Id).c_str(),
                                  static_cast<int>(kind.size()), kind.data(),
                                  link.Enabled ? "enabled" : "DISABLED", link.CostScale,
                                  link.Revision);
            }
            for (const std::string& diagnostic : navigation->Diagnostics())
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", diagnostic.c_str());
        }
        ImGui::PopID();
    }
    ImGui::End();
}
