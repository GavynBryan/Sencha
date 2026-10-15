#include <debug/NavAgentPanel.h>

#include <ecs/World.h>
#include <movement/MovementIntent.h>
#include <navigation/NavAgent.h>
#include <navigation/NavAgentSystem.h>

#include <imgui.h>

#include <cstdio>
#include <string>

namespace
{
    template <typename E>
    const char* EnumLabel(E value)
    {
        const std::string_view name = NavEnumName(value);
        return name.empty() ? "?" : name.data();
    }

    const char* DestinationKindLabel(NavDestinationKind kind)
    {
        switch (kind)
        {
        case NavDestinationKind::None: return "none";
        case NavDestinationKind::Point: return "point";
        case NavDestinationKind::Entity: return "entity";
        }
        return "?";
    }
}

NavAgentPanel::NavAgentPanel(const World& world, NavAgentSystem& agents)
    : Entities(world)
    , Agents(agents)
{
}

void NavAgentPanel::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(520, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Navigation agents"))
    {
        ImGui::End();
        return;
    }

    const NavAgentSystem::Stats& stats = Agents.LastStats();
    ImGui::Text("agents %zu  active %zu  queued %zu  route slots %zu", stats.Agents, stats.Active, stats.Queued,
                Agents.RouteSlotsInUse());
    ImGui::Text("planned %zu%s  repaths %zu  failures %zu  wider than profile %zu", stats.Planned,
                stats.PlannedInParallel ? " (parallel)" : "", stats.Repaths, stats.Failures,
                stats.AgentsWiderThanProfile);

    const EntityId selected = Agents.SelectedAgent();
    ImGui::SeparatorText("Agents");
    for (const EntityId agent : Agents.LastAgents())
    {
        const NavAgentProgress* progress = Entities.TryGet<NavAgentProgress>(agent);
        if (progress == nullptr)
            continue;
        char label[128];
        std::snprintf(label, sizeof(label), "entity %u:%u  %s%s%s", agent.Index, agent.Generation,
                      EnumLabel(progress->Phase), progress->Phase == NavAgentPhase::Failed ? " / " : "",
                      progress->Phase == NavAgentPhase::Failed ? EnumLabel(progress->Failure) : "");
        if (ImGui::Selectable(label, agent == selected))
            Agents.SelectAgent(agent == selected ? EntityId{} : agent);
    }

    const NavAgentProgress* progress = selected.IsValid() ? Entities.TryGet<NavAgentProgress>(selected) : nullptr;
    const NavAgentRequest* order = selected.IsValid() ? Entities.TryGet<NavAgentRequest>(selected) : nullptr;
    if (progress == nullptr || order == nullptr)
    {
        ImGui::TextDisabled("select an agent to see its order and route");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Order");
    ImGui::Text("request %u  kind %s%s  arrive radius %.2f  speed %.2f", order->Id.Value, DestinationKindLabel(order->Kind),
                order->Follow ? " (follow)" : "", order->ArriveRadius, order->SpeedScale);
    if (order->Kind == NavDestinationKind::Point)
        ImGui::Text("point %.2f %.2f %.2f", order->Point.X, order->Point.Y, order->Point.Z);
    else if (order->Kind == NavDestinationKind::Entity)
        ImGui::Text("target %u:%u", order->Target.Index, order->Target.Generation);
    ImGui::Text("phase %s  failure %s  remaining %.2f m", EnumLabel(progress->Phase), EnumLabel(progress->Failure),
                progress->Remaining);
    ImGui::Text("last search %s  repaths %u (last: %s)", NavStatusName(progress->LastQuery), progress->Repaths,
                EnumLabel(progress->LastRepath));

    NavAgentInspection inspection;
    if (Agents.Inspect(selected, inspection))
    {
        ImGui::SeparatorText("Route");
        if (inspection.HasRoute)
            ImGui::Text("step %u of %u (%s)  corner %u of %u", inspection.Step + 1, inspection.StepCount,
                        inspection.Crossing ? "crossing" : "walking", inspection.Corner + 1, inspection.LegCorners);
        else
            ImGui::TextDisabled("no route%s", inspection.Queued ? "; waiting to plan" : "");
        if (inspection.Crossing)
            ImGui::Text("link %016llx  ticks %u  failures %u",
                        static_cast<unsigned long long>(inspection.Link.Value), inspection.CrossingTicks,
                        inspection.CrossingFailures);
        const NavQueryDiagnostics& search = inspection.LastSearch;
        ImGui::Text("last search: %s, %zu nodes%s, cost %.1f, %zu links", NavStatusName(search.Status),
                    search.NodesVisited, search.SearchExhausted ? " (exhausted)" : "", search.Cost,
                    search.LinksTraversed);
        ImGui::Text("stuck window %.2f s, asked %.2f m  stuck replans %u", inspection.StuckElapsed,
                    inspection.StuckExpected, inspection.StuckReplans);
    }
    if (const MovementIntent* intent = Entities.TryGet<MovementIntent>(selected))
        ImGui::Text("intent %.2f %.2f %.2f%s", intent->WishDir.X, intent->WishDir.Y, intent->WishDir.Z,
                    intent->Jump ? " jump" : "");
    ImGui::End();
}
