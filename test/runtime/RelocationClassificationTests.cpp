#include <ecs/WorldComponentSchema.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/relocation/RelocationRules.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

// Every engine component either has a relocation rule or is named here as
// holding no world-space state. A component added without being classified
// fails until someone decides which, so state a relocation would carry wrongly
// cannot arrive silently (docs/gameplay/relocation.md, "State that needs no rule").
namespace
{
// clang-format off
constexpr std::string_view kHoldsNoWorldSpaceState[] = {
    // Derived, or a link to state elsewhere.
    "sencha.parent",
    "persistent_id",
    "scene_instance",
    "sencha.held",
    "sencha.zone_state_lifetime",
    "sencha.kept_with_zone",
    "sencha.relocation_serial",
    // Offsets and extents in the entity's own frame.
    "sencha.presentation_offset",
    "sencha.inherits_presentation_offset",
    "sencha.level_landmark",
    "IrradianceVolume",
    "World Dock",
    "sencha.aim_origin",
    "HearingSensor",
    "SightSensor",
    "SightTarget",
    "sencha.attachment",
    "sencha.attachment_set",
    "sencha.navigation.link",
    // Records of what happened where it happened, which stay there.
    "sencha.ability_cue_log",
    "sencha.active_abilities",
    // Describes the order a relocation reissues.
    "sencha.navigation.agent_progress",
    // Held by another entity about a member; the contact-line group rule ends it.
    "sencha.contact_line_state",
    // No position, direction or heading at all.
    "StaticMesh",
    "SkinnedMesh",
    "ZoneLightmap",
    "PointLight",
    "SpotLight",
    "sencha.mesh_overrides",
    "sencha.presented_with",
    "AudioSource",
    "AudioCaption",
    "World Link",
    "Dock Gate Binding",
    "sencha.navigation.link_state",
    "sencha.navigation.geometry",
    "sencha.navigation.agent",
    "Camera",
    "sencha.camera_exclusion",
    "sencha.camera_subject",
    "sencha.camera_bob",
    "sencha.camera_tilt",
    "sencha.physics.collider",
    "sencha.physics.character_controller",
    "sencha.hit_region",
    "sencha.actor_time",
    "sencha.actor_time_leases",
    "sencha.actor_time_dilated",
    "sencha.gameplay_tag_container",
    "sencha.attribute_set",
    "sencha.active_effect",
    "sencha.applied_effects",
    "sencha.ability_grants",
    "sencha.ability_predicted_cues",
    "sencha.ability_presentation_state",
    "sencha.jump_state",
    "sencha.character_movement",
    "sencha.movement_tuning_source",
    "sencha.resolved_movement_tuning",
    "sencha.mode_transition_request",
    "sencha.locomotion_mode_lease",
    "sencha.stride_cycle",
    "sencha.flight_session",
    "sencha.local_look_control",
    "sencha.aim_facing",
    "sencha.input_action_source_ref",
    "sencha.participant_control",
    "sencha.local_participant",
    "sencha.net_replicated",
    "sencha.net_owner",
    "sencha.net_driven_by",
    "sencha.net_spawn_prefab",
    "sencha.net_participant_identity",
    "sencha.net_relevance_anchor",
    "sencha.verb_relay",
    "sencha.responses",
    "sencha.effect_reactions",
    "sencha.counter",
    "sencha.item_entry",
    "sencha.item_container",
    "sencha.item_pickup",
    "sencha.equipment_state",
    "sencha.anim_rig",
    "sencha.anim_rig_composition",
    "sencha.anim_pose_consumer",
    "sencha.anim_request_set",
    "sencha.anim_presentation_requests",
    "sencha.anim_facts",
    "sencha.anim_facts_large",
    "sencha.anim_fact_history",
    "sencha.anim_fact_subject",
    "sencha.anim_selector_state",
    "sencha.anim_content_state",
    "sencha.anim_event_cursor",
    "sencha.anim_request_report",
    "sencha.anim_flow_state",
    "sencha.anim_pose_state",
    "sencha.anim_decision_log",
    "sencha.observation_sharing",
    "sencha.projectile",
    "sencha.projectile_flight",
    "sencha.contact_line",
    "sencha.contact_line_span",
};
// clang-format on

struct ClassifiedWorld
{
    ClassifiedWorld()
        : Schema(EngineSchema())
        , Runtime(Schema)
    {
        InstallPhysicsBackendRelocation(Runtime.Entities(), Physics, Movers);
    }

    static WorldComponentSchema EngineSchema()
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, nullptr, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    [[nodiscard]] bool HasRule(ComponentId id) const
    {
        const RelocationRules* rules = Runtime.Entities().TryGetResource<RelocationRules>();
        return rules != nullptr && std::ranges::any_of(rules->ComponentRules(),
                                   [&](const RelocationRules::ComponentRule& rule) { return rule.Component == id; });
    }

    WorldComponentSchema Schema;
    PhysicsWorld Physics;
    CharacterMoverPool Movers{ Physics };
    RuntimeWorld Runtime;
};

bool Exempt(std::string_view name)
{
    return std::ranges::find(kHoldsNoWorldSpaceState, name) != std::end(kHoldsNoWorldSpaceState);
}
} // namespace

TEST(RelocationClassification, EveryEngineComponentHasARuleOrIsNamedAsHoldingNoWorldSpaceState)
{
    ClassifiedWorld fx;
    const World& world = fx.Runtime.Entities();
    std::string unclassified;
    std::string both;
    for (const ComponentTypeId type : EngineComponentIds())
    {
        const ComponentId id = world.GetComponentIdByType(type);
        ASSERT_NE(id, InvalidComponentId);
        const std::string_view name = world.GetMeta(id)->Name;
        const bool ruled = fx.HasRule(id);
        if (!ruled && !Exempt(name))
            unclassified += "\n  \"" + std::string(name) + "\",";
        if (ruled && Exempt(name))
            both += "\n  " + std::string(name);
    }
    EXPECT_TRUE(unclassified.empty())
        << "give these a relocation rule, or add them to kHoldsNoWorldSpaceState:" << unclassified;
    EXPECT_TRUE(both.empty()) << "these have a rule and are also listed as holding no world-space state:" << both;
}

TEST(RelocationClassification, EveryNameListedIsAnEngineComponent)
{
    ClassifiedWorld fx;
    const World& world = fx.Runtime.Entities();
    for (const std::string_view listed : kHoldsNoWorldSpaceState)
    {
        const bool known = std::ranges::any_of(EngineComponentIds(), [&](ComponentTypeId type) {
            return world.GetMeta(world.GetComponentIdByType(type))->Name == listed;
        });
        EXPECT_TRUE(known) << listed << " is no engine component: remove it from the list";
    }
}
