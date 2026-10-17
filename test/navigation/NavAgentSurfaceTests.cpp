// What content, a console and a debug view can do with navigation agents:
// the navigation.* verbs and queries, the nav.agent.* commands, the drawing
// and the inspection behind the panel.

#include "NavAgentHarness.h"

#include <app/EngineVocabulary.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <debug/DebugLines.h>
#include <ecs/EntityText.h>
#include <navigation/NavigationVerbs.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    // The engine's vocabulary in the harness world, with the navigation verbs
    // and queries bound as the runtime binds them.
    struct AuthoredNavigation
    {
        NavAgentHarness& Harness;
        std::unique_ptr<VerbDispatcher> Verbs;
        std::unique_ptr<AuthoredQueryDispatcher> Queries;
        NavigationOperations Operations;
        std::vector<VerbBindingToken> VerbTokens;
        std::vector<AuthoredQueryBindingToken> QueryTokens;

        explicit AuthoredNavigation(NavAgentHarness& harness)
            : Harness(harness)
            , Operations(harness.Entities())
        {
            World& world = harness.Entities();
            InstallAuthoredVocabulary(world);
            EXPECT_TRUE(InstallEngineVocabulary(world));
            Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(world));
            Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(world));
            const VerbRegistry& catalog = Verbs->Registry();
            VerbTokens.push_back(Verbs->Bind<&InvokeNavigationMoveTo>(catalog.Find(kNavigationMoveToVerb), Operations));
            VerbTokens.push_back(Verbs->Bind<&InvokeNavigationStop>(catalog.Find(kNavigationStopVerb), Operations));
            QueryTokens = BindNavigationQueries(*Queries, world);
        }

        VerbAdmission Invoke(std::string_view verb, std::vector<std::string> inputs, std::vector<AuthoredValue> values)
        {
            VerbBindingDesc desc;
            desc.Key = std::string(verb) + ".test";
            desc.KeyId = MakeVerbBindingKey(desc.Key);
            desc.VerbName = std::string(verb);
            desc.Inputs = inputs;
            for (const std::string& input : inputs)
            {
                VerbBindingArgument argument;
                argument.Key = input;
                argument.Source = VerbArgumentSource::Input;
                argument.Text = input;
                desc.Arguments.push_back(argument);
            }
            CompiledVerbBinding binding;
            std::vector<std::string> errors;
            EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Harness.Entities()), binding, errors))
                << (errors.empty() ? "" : errors.front());
            return Verbs->Invoke(binding, values).Status;
        }

        std::string AskEnum(std::string_view query, EntityId agent)
        {
            AuthoredValue answer;
            const AuthoredValue argument = AuthoredValue::Entity(agent);
            if (Queries->Evaluate(Queries->Registry().Resolve(query), std::span(&argument, 1), answer)
                != AuthoredQueryStatus::Value)
                return "unavailable";
            std::string_view choice;
            return answer.TryGetEnum(choice) ? std::string(choice) : "not an enum";
        }
    };

    AuthoredValue PointArgument(float x, float y, float z)
    {
        AuthoredVectorValue vector;
        vector.Components = { x, y, z, 0.0 };
        return AuthoredValue::Vector(vector);
    }

    std::string TextOfEntity(EntityId entity)
    {
        std::string text;
        AppendEntityText(text, entity);
        return text;
    }

    std::string JoinedOutput(const ConsoleResult& result)
    {
        std::string text;
        for (const ConsoleOutputEntry& entry : result.Output)
            text += entry.Text + "\n";
        return text;
    }
}

TEST(NavAgentSurface, AuthoredContentOrdersAnAgentAndReadsWhatBecameOfIt)
{
    NavAgentHarness harness(LongHall());
    AuthoredNavigation authored(harness);
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));

    EXPECT_EQ(authored.AskEnum(kNavigationPhaseQuery, agent), "idle");
    EXPECT_EQ(authored.Invoke(kNavigationMoveToVerb, { "target", "point" },
                              { AuthoredValue::Entity(agent), PointArgument(12.0f, 0.0f, 5.0f) }),
              VerbAdmission::Accepted);
    harness.Step();
    EXPECT_EQ(authored.AskEnum(kNavigationPhaseQuery, agent), "moving");
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 20));
    EXPECT_EQ(authored.AskEnum(kNavigationPhaseQuery, agent), "arrived");
    EXPECT_EQ(authored.AskEnum(kNavigationFailureQuery, agent), "none");

    EXPECT_EQ(authored.Invoke(kNavigationStopVerb, { "target" }, { AuthoredValue::Entity(agent) }),
              VerbAdmission::Accepted);
    harness.Step();
    EXPECT_EQ(authored.AskEnum(kNavigationPhaseQuery, agent), "cancelled");

    const EntityId bystander = harness.SpawnMarker(Vec3d(5.0f, 0.0f, 5.0f));
    EXPECT_EQ(authored.Invoke(kNavigationStopVerb, { "target" }, { AuthoredValue::Entity(bystander) }),
              VerbAdmission::Refused);
    EXPECT_EQ(authored.AskEnum(kNavigationPhaseQuery, bystander), "unavailable");
}

TEST(NavAgentSurface, TheConsoleOrdersAndListsAgents)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    const std::string name = TextOfEntity(agent);

    EXPECT_FALSE(harness.Console.ExecuteLine("nav.agent.goto " + name + " 12 nowhere 5").Succeeded());
    EXPECT_FALSE(harness.Console.ExecuteLine("nav.agent.goto 99:1 12 0 5").Succeeded());
    const ConsoleResult sent = harness.Console.ExecuteLine("nav.agent.goto " + name + " 12 0 5");
    ASSERT_TRUE(sent.Succeeded()) << JoinedOutput(sent);
    harness.Step();
    EXPECT_NE(JoinedOutput(harness.Console.ExecuteLine("nav.agent.list")).find(name + " moving request 1"),
              std::string::npos);

    ASSERT_TRUE(harness.Console.ExecuteLine("nav.agent.stop " + name).Succeeded());
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Cancelled);

    const EntityId leader = harness.SpawnMarker(Vec3d(20.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.Console.ExecuteLine("nav.agent.follow " + name + " " + TextOfEntity(leader)).Succeeded());
    ASSERT_TRUE(harness.StepUntil([&] { return harness.Progress(agent).Phase == NavAgentPhase::Holding; }, 60 * 20));
}

TEST(NavAgentSurface, DrawingShowsRoutesAndTheSelectedAgentOverTheScene)
{
    NavAgentHarness harness(WallRoom());
    const EntityId walker = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    const EntityId other = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 6.0f));
    (void)harness.GoTo(walker, Vec3d(17.0f, 0.0f, 3.0f));
    (void)harness.GoTo(other, Vec3d(17.0f, 0.0f, 6.0f));
    harness.Step();
    EXPECT_FALSE(harness.Entities().HasResource<DebugLineChannels>()) << "off by default, and costs nothing";

    harness.SetCVar("nav.agent.debug.draw", true);
    harness.Agents().SelectAgent(walker);
    harness.Step();
    DebugLineChannels& channels = harness.Entities().GetResource<DebugLineChannels>();
    const DebugLineList& lines = channels.Lines(channels.Channel("navigation.agents"));
    EXPECT_FALSE(lines.Tested().empty()) << "the other agent's route";
    EXPECT_FALSE(lines.OnTop().empty()) << "the selected agent's route and wish";

    harness.SetCVar("nav.agent.debug.draw", false);
    harness.Step();
    EXPECT_TRUE(channels.Lines(channels.Channel("navigation.agents")).Empty());
}

TEST(NavAgentSurface, InspectionShowsTheRouteCursorAndLastSearch)
{
    NavAgentHarness harness(WallRoom());
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    (void)harness.GoTo(agent, Vec3d(17.0f, 0.0f, 3.0f));
    for (int i = 0; i < 30; ++i)
        harness.Step();
    NavAgentInspection inspection;
    ASSERT_TRUE(harness.Agents().Inspect(agent, inspection));
    EXPECT_TRUE(inspection.HasRoute);
    EXPECT_FALSE(inspection.Crossing);
    EXPECT_EQ(inspection.StepCount, 1u);
    EXPECT_GE(inspection.LegCorners, 2u) << "around the wall";
    EXPECT_EQ(inspection.LastSearch.Status, NavStatus::Success);
    EXPECT_GT(inspection.LastSearch.NodesVisited, 0u);

    NavAgentInspection none;
    EXPECT_FALSE(harness.Agents().Inspect(harness.SpawnMarker(Vec3d(1.0f, 0.0f, 1.0f)), none));
}
