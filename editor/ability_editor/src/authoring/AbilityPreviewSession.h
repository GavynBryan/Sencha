#pragma once

#include "preview/FixedTickPreview.h"

#include <abilities/AbilityId.h>
#include <abilities/AbilityTrace.h>
#include <abilities/ActiveAbilities.h>
#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionSet.h>
#include <input/InputActionRegistry.h>
#include <time/ActorTime.h>
#include <time/SimClock.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AuthoredQueryDispatcher;
class EngineVocabularyBindings;
class PhysicsWorld;
class VerbDispatcher;
class World;
struct AbilityDefinition;
struct RuntimeAssets;

// One thing done to the fixture on a tick.
enum class AbilityPreviewControl : std::uint8_t
{
    Press,
    Release,
    // Released by something other than the person: a menu, a lost focus.
    Interrupt,
    // Pressed and released Count times within the tick.
    Tap,
    // Name's base set to Value.
    SetAttribute,
    GrantTag,
    RevokeTag,
    // The fixture's time scaled by Value from the next tick; one lets it go.
    TimeScale,
    // A struck body with every attribute, Value metres along the fixture's aim.
    PlaceTarget,
};

struct AbilityPreviewAction
{
    std::uint64_t Tick = 0;
    AbilityPreviewControl Control = AbilityPreviewControl::Press;
    // The input action, attribute or tag the control names.
    std::string Name = {};
    double Value = 0.0;
    std::uint32_t Count = 1;

    friend bool operator==(const AbilityPreviewAction&, const AbilityPreviewAction&) = default;
};

// What one tick of the preview did, as the inspector and the deck read it.
struct AbilityPreviewTickRecord
{
    std::uint64_t Tick = 0;
    std::vector<AbilityPreviewAction> Applied;
    // The fixture's, in the order the pass recorded them.
    std::vector<AbilityTraceRecord> Trace;
    // The fixture's occupied records after the tick.
    std::vector<AbilityRecord> Records;
    // Each attribute's current value, by attribute id from one.
    std::vector<float> Attributes;
    std::int64_t ScaleQ16 = kActorTimeOne;
};

//=============================================================================
// AbilityPreviewSession
// One ability run on a fixture pawn in a private World with the production
// schedule and the engine's own operations, on the shared preview clock. The
// controls are a timed script: rewinding replays it from tick zero, and acting
// at a past tick cuts what came after (docs/editor/ability-workbench.md).
//=============================================================================
class AbilityPreviewSession
{
public:
    // `vocabulary` adds a loaded game module's names to each preview World.
    AbilityPreviewSession(RuntimeAssets& assets, std::function<void(World&)> vocabulary);
    ~AbilityPreviewSession();

    AbilityPreviewSession(const AbilityPreviewSession&) = delete;
    AbilityPreviewSession& operator=(const AbilityPreviewSession&) = delete;

    // The fixture granted the ability `data` describes, replayed to the current
    // tick; called again whenever the working data changes.
    void Show(std::string path, JsonValue data);
    void Close();

    void Restart() { Clock.Restart(); }
    void Step() { Clock.Step(); }
    void RunTo(std::uint64_t tick) { Clock.RunTo(tick); }
    void Advance(double wallSeconds) { Clock.Advance(wallSeconds, kTickRate); }
    void Play() { Clock.Play(); }
    void Pause() { Clock.Pause(); }
    [[nodiscard]] bool SetSpeed(double speed) { return Clock.SetSpeed(speed); }
    [[nodiscard]] bool IsPlaying() const { return Clock.IsPlaying(); }
    [[nodiscard]] double Speed() const { return Clock.Speed(); }
    [[nodiscard]] std::uint64_t Tick() const { return Clock.Now(); }
    [[nodiscard]] const std::deque<AbilityPreviewTickRecord>& History() const { return Clock.History(); }

    // A control applied on the next tick; at a past tick, what came after it goes.
    void Control(AbilityPreviewAction action);
    [[nodiscard]] const std::vector<AbilityPreviewAction>& Script() const { return Actions; }

    [[nodiscard]] bool IsOpen() const { return Preview != nullptr; }
    // Why the preview cannot run the ability as it stands; empty when it can.
    [[nodiscard]] const std::vector<std::string>& Problems() const { return Issues; }
    // The input actions the ability's binding names, which the controls press.
    [[nodiscard]] const std::vector<std::string>& InputActions() const { return ActionNames; }
    [[nodiscard]] World* SimulationWorld() { return Preview.get(); }
    [[nodiscard]] const World* SimulationWorld() const { return Preview.get(); }
    [[nodiscard]] EntityId Fixture() const { return FixtureEntity; }
    [[nodiscard]] AbilityId Ability() const { return Shown; }
    [[nodiscard]] const AbilityDefinition* Definition() const;

    static constexpr std::uint32_t kTickRate = 60;

private:
    bool Rebuild();
    void DropWorld();
    AbilityPreviewTickRecord RunTick(std::uint64_t tick);
    void Apply(const AbilityPreviewAction& action, std::uint64_t tick);

    RuntimeAssets& Assets;
    std::function<void(World&)> Vocabulary;
    std::string Path;
    JsonValue Data;
    std::vector<AbilityPreviewAction> Actions;
    std::vector<std::string> Issues;
    std::vector<std::string> ActionNames;

    // Member order is teardown order: the bindings and systems go before the
    // dispatchers and physics they hold, and those before the World.
    std::unique_ptr<World> Preview;
    std::unique_ptr<PhysicsWorld> Physics;
    FixedSimulationLoop SimulationClock{ 1.0 / kTickRate };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    struct Systems;
    std::unique_ptr<Systems> Schedule;
    InputActionRegistry ActionSet;
    std::vector<bool> Held;
    EntityId FixtureEntity;
    AbilityId Shown;
    StoragePartitionSet AllPartitions;

    FixedTickPreview<AbilityPreviewTickRecord> Clock{ { .Rebuild = [this] { return Rebuild(); },
                                                        .CanRun = [this] { return Preview != nullptr && Shown.IsValid(); },
                                                        .Run = [this](std::uint64_t tick) { return RunTick(tick); } } };
};
