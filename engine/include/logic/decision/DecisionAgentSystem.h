#pragma once

#include <assets/data/DataAssetLease.h>
#include <ecs/EntityId.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <logic/decision/DecisionAgent.h>
#include <logic/decision/DecisionProfile.h>
#include <logic/selection/SelectionEvaluator.h>
#include <logic/selection/SelectionTable.h>
#include <math/Vec.h>
#include <perception/ObservationMemory.h>
#include <spatial/candidates/CandidateTrace.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AuthoredQueryDispatcher;
class Logger;
class World;
struct FixedLogicContext;
struct RuntimeAssets;

// What an input reads when the agent decides.
struct DecisionMoment
{
    EntityId Self;
    const DecisionAgent& Agent;
    Vec3d Position = Vec3d::Zero();
    // The target as memory has it; null once memory has let go.
    const ObservationMemoryEntry* Target = nullptr;
};

using DecisionInput = std::function<float(const DecisionMoment&)>;

enum class DecisionTurn : std::uint8_t
{
    // Its clocks run and it decides when they say so.
    Decide,
    // Its clocks run but it does not decide this tick.
    Hold,
    // Left alone entirely this tick: it is leaving.
    Skip,
};

struct DecisionPlace
{
    Vec3d Position = Vec3d::Zero();
    float Score = 0.0f;
};

// The last decision an agent made, for describing and drawing it.
struct DecisionRecord
{
    EntityId Agent{};
    SelectionTrace Trace{};
    std::string Table{};
    std::string Note{};
    std::optional<CandidateTrace> Places{};
    // The last place run that found nowhere, kept past the runs after it.
    std::optional<CandidateTrace> Nowhere{};
};

// What the game adds to a decision. Each is optional.
struct DecisionHooks
{
    // The profile the agent decides by; null leaves it alone.
    std::function<const DecisionProfile*(const World&, EntityId)> ProfileOf{};
    // A profile's table bound, or bound again: what the game prepares per profile.
    std::function<void(const DecisionProfile&)> Bound{};
    // Every tick before its clocks run: the game's own clocks, and whether it decides.
    std::function<DecisionTurn(World&, EntityId, float elapsed)> Turn{};
    // It has a target again after having none.
    std::function<void(World&, EntityId)> TargetFound{};
    // Once the target is known, before anything is measured.
    std::function<void(World&, EntityId, DecisionAgent&, const ObservationMemoryEntry* target, const Vec3d& position)>
        Prepare{};
    // The ability of the option it was running has ended.
    std::function<void(World&, EntityId, std::string_view option)> OptionEnded{};
    // It chose an option, or chose a new place for the one it holds.
    std::function<void(World&, EntityId, std::string_view option, bool changed)> OptionChosen{};
    // Nothing qualified, so it stopped.
    std::function<void(World&, EntityId)> Stopped{};
    // Where the option goes; Ran says whether an evaluation ran, which spends the tick's budget.
    struct PlaceChoice
    {
        std::optional<DecisionPlace> Place;
        bool Ran = false;
    };
    std::function<PlaceChoice(World&, EntityId, DecisionAgent&, const DecisionOptionPlan&,
                              const ObservationMemoryEntry* target, std::uint64_t tick, DecisionRecord&)>
        ChoosePlace{};
};

struct DecisionRunSettings
{
    bool Frozen = false;
    bool Drawing = false;
    // Place evaluations the agents may run between them in one tick.
    std::uint32_t PlaceRuns = 4;
    std::string_view DebugChannel = "decision";
};

// Decides for every DecisionAgent on its own clock, on the authority: a target
// from memory, an option from the profile's selection table, a place for it, and
// its ability requested by name. The game owns the system that calls Run, and
// supplies inputs by name and hooks for what it adds. docs/gameplay/decision-agent.md.
class DecisionAgents
{
public:
    struct Services
    {
        RuntimeAssets* Assets = nullptr;
        const AuthoredQueryDispatcher* Queries = nullptr;
        Logger* Log = nullptr;
    };

    DecisionAgents(Services services, DecisionHooks hooks);
    ~DecisionAgents();
    DecisionAgents(const DecisionAgents&) = delete;
    DecisionAgents& operator=(const DecisionAgents&) = delete;

    // What a table's input of this name reads.
    void RegisterInput(std::string name, DecisionInput input);

    void Run(FixedLogicContext& ctx, const DecisionRunSettings& settings);
    // Releases every asset lease; decisions resume once the next tick binds again.
    void Close();

    // Whether a decision has bound the profile's table yet.
    [[nodiscard]] bool HasBound(const DecisionProfile& profile) const;
    // The option's name in the profile's table, binding the table first if no
    // decision has; empty when it does not bind or has no such option.
    [[nodiscard]] std::string_view OptionName(const World& world, const DecisionProfile& profile, std::uint32_t option);
    // The group an agent decides with: its ObservationSharing group.
    [[nodiscard]] static GameplayTagId GroupOf(const World& world, EntityId agent);
    // Places the group has marked visited within the last `seconds`.
    void CollectVisited(GameplayTagId group, float seconds, std::vector<Vec3d>& out) const;

    [[nodiscard]] std::string Describe(EntityId agent) const;
    [[nodiscard]] std::string DescribeCost() const;

private:
    struct State;
    std::unique_ptr<State> Impl;
};
