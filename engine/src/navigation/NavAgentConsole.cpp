#include "NavAgentConsole.h"

#include <core/console/ConsoleRegistry.h>
#include <ecs/EntityText.h>
#include <ecs/World.h>
#include <navigation/NavAgentRequests.h>
#include <participant/LocalControl.h>
#include <world/transform/TransformComponents.h>

#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace
{
    std::optional<EntityId> ParseAgentArgument(const World& world, std::string_view text)
    {
        const std::optional<EntityId> entity = ParseEntityText(text);
        if (!entity || !world.IsRegistered<NavAgent>() || world.TryGet<NavAgent>(*entity) == nullptr)
            return std::nullopt;
        return entity;
    }

    // "me" is the entity this machine's player controls; anything else is index:generation.
    std::optional<EntityId> ParseSubjectArgument(const World& world, std::string_view text)
    {
        const EntityId entity = text == "me" ? LocalControlSubjectOf(world) : ParseEntityText(text).value_or(EntityId{});
        return world.IsAlive(entity) ? std::optional<EntityId>(entity) : std::nullopt;
    }

    std::optional<float> ParseCoordinate(std::string_view text)
    {
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? std::optional<float>(value) : std::nullopt;
    }

    ConsoleResult RefuseArguments(std::string message)
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error(std::move(message));
        return result;
    }

    ConsoleResult ReportIssuedRequest(std::string_view agent, NavRequestId request)
    {
        ConsoleResult result;
        result.Info(std::format("{} has request {}", agent, request.Value));
        return result;
    }
}

void RegisterNavigationAgentConsole(ConsoleRegistry& console, World& world)
{
    (void)console.RegisterCommand({
        .Name = "nav.agent.list",
        .Owner = "navigation",
        .Usage = "nav.agent.list",
        .Help = "Lists the navigation agents as index:generation with where their orders stand.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string>) {
            ConsoleResult result;
            if (!world.IsRegistered<NavAgentProgress>())
            {
                result.Info("no navigation agents");
                return result;
            }
            const World& reader = world;
            reader.ForEachComponent<NavAgentProgress>([&](EntityId entity, const NavAgentProgress& progress) {
                std::string line;
                AppendEntityText(line, entity);
                line += std::format(" {} request {}", NavEnumName(progress.Phase), progress.Request.Value);
                if (progress.Phase == NavAgentPhase::Failed)
                    line += std::format(" ({})", NavEnumName(progress.Failure));
                result.Info(std::move(line));
            });
            return result;
        },
    });

    (void)console.RegisterCommand({
        .Name = "nav.agent.goto",
        .Owner = "navigation",
        .Usage = "nav.agent.goto <agent> <x> <y> <z> | nav.agent.goto <agent> me",
        .Help = "Sends a navigation agent to a point, or to where this machine's player stands.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::optional<EntityId> agent = args.empty() ? std::nullopt : ParseAgentArgument(world, args[0]);
            if (!agent)
                return RefuseArguments("name an agent as index:generation; nav.agent.list lists them");
            NavDestination destination;
            if (args.size() == 2)
            {
                const std::optional<EntityId> subject = ParseSubjectArgument(world, args[1]);
                const WorldTransform* pose = subject && world.IsRegistered<WorldTransform>()
                    ? std::as_const(world).TryGet<WorldTransform>(*subject)
                    : nullptr;
                if (pose == nullptr)
                    return RefuseArguments(std::format("'{}' is not somewhere an agent can go", args[1]));
                destination = NavDestination::AtPoint(pose->Value.Position);
            }
            else if (args.size() == 4)
            {
                const std::optional<float> x = ParseCoordinate(args[1]);
                const std::optional<float> y = ParseCoordinate(args[2]);
                const std::optional<float> z = ParseCoordinate(args[3]);
                if (!x || !y || !z)
                    return RefuseArguments("a point is three numbers");
                destination = NavDestination::AtPoint(Vec3d(*x, *y, *z));
            }
            else
                return RefuseArguments("nav.agent.goto <agent> <x> <y> <z>, or nav.agent.goto <agent> me");
            return ReportIssuedRequest(args[0], RequestNavigation(world, *agent, destination));
        },
    });

    (void)console.RegisterCommand({
        .Name = "nav.agent.follow",
        .Owner = "navigation",
        .Usage = "nav.agent.follow <agent> <entity|me>",
        .Help = "Has a navigation agent keep up with an entity, or with this machine's player.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::optional<EntityId> agent = args.size() == 2 ? ParseAgentArgument(world, args[0]) : std::nullopt;
            const std::optional<EntityId> subject = args.size() == 2 ? ParseSubjectArgument(world, args[1]) : std::nullopt;
            if (!agent || !subject)
                return RefuseArguments("nav.agent.follow <agent> <entity|me>, entities as index:generation");
            return ReportIssuedRequest(args[0], RequestNavigation(world, *agent, NavDestination::AtEntity(*subject, true)));
        },
    });

    (void)console.RegisterCommand({
        .Name = "nav.agent.stop",
        .Owner = "navigation",
        .Usage = "nav.agent.stop <agent>",
        .Help = "Ends a navigation agent's order.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::optional<EntityId> agent = args.size() == 1 ? ParseAgentArgument(world, args[0]) : std::nullopt;
            if (!agent)
                return RefuseArguments("name an agent as index:generation; nav.agent.list lists them");
            return ReportIssuedRequest(args[0], CancelNavigation(world, *agent));
        },
    });
}
