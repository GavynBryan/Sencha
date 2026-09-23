#pragma once

// A World with the animation vocabulary and a data cache the animation data
// subtypes compile into, for tests that bind real rig content. Content is
// written as the JSON an author would write and compiled by the registered
// compilers, so every test goes through the same parse and validation.

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimContentSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimFlowData.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipCache.h>
#include <anim/AnimationRegistration.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbBindingData.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/ComponentTypeId.h>
#include <world/ComponentRegistrar.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>

// Gameplay the tests drive facts from, through bound providers.
struct AnimTestMotion
{
    float Speed = 0.0f;
    bool Grounded = true;
    bool Crouched = false;
    bool Dead = false;
};

SENCHA_DECLARE_COMPONENT_TYPE(AnimTestMotion, "test.anim_motion");

struct AnimRigFixture
{
    static constexpr double kTick = 1.0 / 60.0;

    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    DataAssetCache Data;
    AnimationClipCache Clips;
    World Entities;
    AnimFactGatherSystem Gather;
    AnimSelectSystem Select;
    AnimContentSystem Content;
    AnimTick Now = 0;

    explicit AnimRigFixture(std::initializer_list<const char*> tags = {})
    {
        RegisterAnimFactSchema(Types, Schemas);
        RegisterAnimRequestSchema(Types, Schemas);
        RegisterAnimRigData(Types, Schemas);
        RegisterAnimBehaviorSet(Types, Schemas);
        RegisterAnimSelectorData(Types, Schemas);
        RegisterAnimSlotMapData(Types, Schemas);
        RegisterAnimFlowData(Types, Schemas);
        RegisterVerbBindingData(Types, Schemas);

        Entities.AddResource<GameplayTagRegistry>();
        (void)InstallVerbRegistry(Entities);
        ComponentRegistrar registrar(Entities);
        RegisterAnimationComponents(registrar);
        Entities.RegisterComponent<GameplayTagContainer>();
        Entities.RegisterComponent<AnimTestMotion>();
        InstallAnimationVocabulary(Entities);
        AnimFactProviders& providers = Entities.GetResource<AnimFactProviders>();
        (void)providers.BindField<&AnimTestMotion::Speed>("Speed");
        (void)providers.BindField<&AnimTestMotion::Grounded>("Grounded");
        (void)providers.BindField<&AnimTestMotion::Crouched>("Crouched");
        (void)providers.BindField<&AnimTestMotion::Dead>("Dead");
        for (const char* tag : tags)
            (void)Tags().RegisterTag(tag);
        Entities.SetResource(AnimRigBindings{ &Data, &Clips });
    }

    GameplayTagRegistry& Tags() { return Entities.GetResource<GameplayTagRegistry>(); }
    GameplayTagId Tag(std::string_view name) { return Tags().FindTag(name); }
    AnimRigBindings& Bindings() { return Entities.GetResource<AnimRigBindings>(); }

    DataAssetHandle Load(std::string_view path, std::string_view type, std::string_view json)
    {
        const std::optional<JsonValue> parsed = JsonParse(json);
        EXPECT_TRUE(parsed.has_value()) << path;
        const DataAssetCompileResult compiled = Types.Find(type)->Compile(*parsed);
        EXPECT_TRUE(compiled.IsValid()) << path << ": " << compiled.Error;
        return Data.Register(path, std::string(type), compiled.Value);
    }

    // The compile error for `json`, empty when it compiles.
    std::string CompileError(std::string_view type, std::string_view json)
    {
        return Types.Find(type)->Compile(*JsonParse(json)).Error;
    }

    void Reload(std::string_view path, std::string_view type, std::string_view json)
    {
        const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
        ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
        ASSERT_TRUE(Data.ReloadInPlace(path, type, compiled.Value));
    }

    void Clip(std::string_view path, float seconds, std::vector<AnimationClipEvent> events = {})
    {
        AnimationClipData clip;
        clip.DurationSeconds = seconds;
        clip.Events = std::move(events);
        (void)Clips.Register(path, std::move(clip), {});
    }

    VerbRegistry& Verbs() { return *FindVerbRegistry(Entities); }

    const AnimBoundRig& Bound(DataAssetHandle rig)
    {
        const AnimBoundRig* bound = Bindings().Resolve(rig, Entities);
        EXPECT_NE(bound, nullptr);
        return *bound;
    }

    static std::string Describe(const AnimBoundRig& rig)
    {
        std::string text;
        for (const AnimDiagnostic& diagnostic : rig.Diagnostics)
            text += FormatAnimDiagnostic(diagnostic) + "\n";
        return text;
    }

    static const AnimDiagnostic* FindCode(const AnimBoundRig& rig, std::string_view code)
    {
        for (const AnimDiagnostic& diagnostic : rig.Diagnostics)
            if (diagnostic.Code == code)
                return &diagnostic;
        return nullptr;
    }

    // Gather, select and resolve for tick Now, then advance it.
    void Tick()
    {
        Gather.Gather(Entities, Now, kTick);
        Select.Select(Entities, Now, kTick);
        Content.Resolve(Entities, Now, kTick);
        ++Now;
    }

    void Tick(int count)
    {
        for (int i = 0; i < count; ++i)
            Tick();
    }

    // A Simple-tier character: the rig, small facts (which bring history and
    // selector state), motion to gather from, and a decision log.
    EntityId Character(DataAssetHandle rig, AnimTestMotion motion = {})
    {
        const EntityId entity = Entities.CreateEntity();
        Entities.AddComponent(entity, AnimRig{ rig });
        Entities.AddComponent(entity, AnimFacts{});
        Entities.AddComponent(entity, motion);
        Entities.AddComponent(entity, AnimDecisionLog{});
        return entity;
    }

    AnimTestMotion& Motion(EntityId entity) { return *Entities.TryGet<AnimTestMotion>(entity); }
    const AnimLayerSelection& Selection(EntityId entity, std::size_t layer = 0)
    {
        return Entities.TryGet<AnimSelectorState>(entity)->Layers[layer];
    }
    const AnimLayerContent& Playing(EntityId entity, std::size_t layer = 0)
    {
        return Entities.TryGet<AnimContentState>(entity)->Layers[layer];
    }
    std::string BehaviorName(EntityId entity, std::size_t layer = 0)
    {
        return std::string(Tags().GetName(Playing(entity, layer).Behavior));
    }
    std::string ClipName(EntityId entity, const AnimBoundRig& rig, std::size_t layer = 0)
    {
        const std::uint16_t content = Playing(entity, layer).Content;
        return content < rig.Contents.size() ? rig.Contents[content].Path : std::string("(none)");
    }
    const AnimDecisionLog& Log(EntityId entity) { return *Entities.TryGet<AnimDecisionLog>(entity); }

    // The most recent record of `cause`, or null.
    const AnimDecisionRecord* LastRecord(EntityId entity, AnimDecisionCause cause)
    {
        const AnimDecisionLog& log = Log(entity);
        for (std::size_t i = log.Size(); i-- > 0;)
            if (log.At(i).Cause == cause)
                return &log.At(i);
        return nullptr;
    }

    AnimRequestResult Issue(EntityId animated, std::string_view intent,
                            AnimRequestLifetime lifetime = AnimRequestLifetime::Held)
    {
        AnimRequestDesc desc;
        desc.Source = animated;
        desc.Intent = Tag(intent);
        desc.Lifetime = lifetime;
        return IssueAnimRequest(Entities, animated, desc, Now);
    }

    // The tick the last Tick() ran.
    [[nodiscard]] AnimTick Last() const { return Now - 1; }
};

// A character rig exercising every Stage 2 mechanism: a locomotion selector
// with hysteresis, a one-shot latched until complete, a request latch, a death
// band that interrupts everything, and a slot map with a stance row.
namespace AnimHero
{
    inline constexpr const char* kTags[] = {
        "anim.intent.reload", "anim.intent.door_open", "stance.crouch",
        "Anim.Locomotion.Idle", "Anim.Locomotion.Walk", "Anim.Locomotion.Sprint",
        "Anim.Action.Land", "Anim.Action.Reload", "Anim.Death",
    };

    inline constexpr std::string_view kFacts = R"({
        "slots": [ { "name": "Grounded", "kind": "bool" }, { "name": "Speed", "kind": "float" },
                   { "name": "Crouched", "kind": "bool" }, { "name": "Dead", "kind": "bool" } ],
        "derived": [ { "name": "JustLanded", "op": "edge", "source": { "fact": "Grounded" },
                       "direction": "rising", "window_ms": 100 } ] })";

    inline constexpr std::string_view kRequests = R"({
        "intents": [ { "intent": "anim.intent.reload", "params": [ { "name": "rate", "kind": "float" } ] } ] })";

    inline constexpr std::string_view kBehaviors = R"({ "behaviors": [
        { "tag": "Anim.Locomotion.Idle", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Walk", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Sprint", "kind": "cyclic" },
        { "tag": "Anim.Action.Land", "kind": "one_shot",
          "latch": { "mode": "until_complete", "interruptible_by": "priority_at_least", "priority": 100 } },
        { "tag": "Anim.Action.Reload", "kind": "one_shot",
          "latch": { "mode": "until_request_ends", "interruptible_by": "tags", "tags": [ "Anim.Death" ],
                     "on_request_cancel": "finish" } },
        { "tag": "Anim.Death", "kind": "hold" } ] })";

    inline constexpr std::string_view kSelector = R"({ "rules": [
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Locomotion.Idle" },
        { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
          "behavior": "Anim.Locomotion.Walk" },
        { "name": "sprint", "priority": 20, "enter": [ { "fact": "Speed", "compare": "gt", "value": 2.2 } ],
          "stay": [ { "fact": "Speed", "compare": "gt", "value": 1.8 } ], "behavior": "Anim.Locomotion.Sprint" },
        { "name": "land", "priority": 40, "enter": [ { "fact": "JustLanded" } ], "behavior": "Anim.Action.Land" },
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
          "behavior": "Anim.Action.Reload" },
        { "name": "death", "priority": 100, "enter": [ { "fact": "Dead" } ], "behavior": "Anim.Death" } ] })";

    inline constexpr std::string_view kSlots = R"({ "rows": [
        { "behavior": "Anim.Locomotion.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "Anim.Locomotion.Walk", "when": [ { "fact": "Crouched" } ],
          "clip": "asset://anim/walk_crouch.sanim" },
        { "behavior": "Anim.Locomotion.Walk", "clip": "asset://anim/walk.sanim" },
        { "behavior": "Anim.Locomotion.Sprint", "clip": "asset://anim/sprint.sanim" },
        { "behavior": "Anim.Action.Land", "when": [ { "fact": "Crouched" } ],
          "clip": "asset://anim/land_crouch.sanim" },
        { "behavior": "Anim.Action.Land", "clip": "asset://anim/land.sanim" },
        { "behavior": "Anim.Action.Reload", "clip": "asset://anim/reload.sanim" },
        { "behavior": "Anim.Death", "clip": "asset://anim/death.sanim" } ] })";

    inline constexpr std::string_view kRig = R"({
        "facts": "asset://anim/hero.facts.sdata",
        "requests": "asset://anim/hero.requests.sdata",
        "behaviors": [ "asset://anim/hero.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/hero.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/hero.selector.sdata",
                      "idle": "Anim.Locomotion.Idle" } ] })";

    // Loads the hero content and returns its rig.
    inline DataAssetHandle Load(AnimRigFixture& fx)
    {
        fx.Clip("asset://anim/idle.sanim", 2.0f);
        fx.Clip("asset://anim/walk.sanim", 1.0f);
        fx.Clip("asset://anim/walk_crouch.sanim", 1.2f);
        fx.Clip("asset://anim/sprint.sanim", 0.8f);
        fx.Clip("asset://anim/land.sanim", 0.25f);
        fx.Clip("asset://anim/land_crouch.sanim", 0.4f);
        fx.Clip("asset://anim/reload.sanim", 1.0f);
        fx.Clip("asset://anim/death.sanim", 1.5f);
        (void)fx.Load("asset://anim/hero.facts.sdata", kAnimFactSchemaType, kFacts);
        (void)fx.Load("asset://anim/hero.requests.sdata", kAnimRequestSchemaType, kRequests);
        (void)fx.Load("asset://anim/hero.behaviors.sdata", kAnimBehaviorSetType, kBehaviors);
        (void)fx.Load("asset://anim/hero.selector.sdata", kAnimSelectorType, kSelector);
        (void)fx.Load("asset://anim/hero.slots.sdata", kAnimSlotMapType, kSlots);
        return fx.Load("asset://anim/hero.rig.sdata", kAnimRigType, kRig);
    }

    inline void RegisterTags(AnimRigFixture& fx)
    {
        for (const char* tag : kTags)
            (void)fx.Tags().RegisterTag(tag);
    }
}
