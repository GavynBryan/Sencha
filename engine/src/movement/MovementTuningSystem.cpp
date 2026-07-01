#include <movement/MovementTuningSystem.h>

#include <app/GameContexts.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementProfile.h>
#include <movement/MovementTags.h>
#include <world/registry/Registry.h>

#include <utility>
#include <variant>

void MovementTuningSystem::Init()
{
    ConsoleRegistry& cvars = Console->Registry();
    const auto feel = [&cvars](const char* name, double def, const char* help)
    {
        CVarMetadata cvar;
        cvar.Name = name;
        cvar.Owner = "gameplay";
        cvar.Type = CVarType::Double;
        cvar.DefaultValue = def;
        cvar.CurrentValue = def;
        cvar.Flags = CVarFlags::Archive;
        cvar.Help = help;
        cvar.Source.Description = "gameplay";
        cvars.RegisterCVar(std::move(cvar));
    };
    feel("movement.ground_accel", 10.0, "Ground acceleration coefficient (PM_Accelerate).");
    feel("movement.air_accel", 10.0, "Air acceleration coefficient (PM_AirAccelerate).");
    feel("movement.friction", 6.0, "Ground friction coefficient.");
    feel("movement.stop_speed", 1.0, "Friction control floor (m/s): crisp low-speed stop.");
    feel("movement.max_air_speed", 1.0, "Wish-speed cap for the air accel term (the air-strafe knob).");
    feel("movement.jump_speed", 5.5, "Jump launch speed (m/s).");
}

void MovementTuningSystem::FixedLogic(FixedLogicContext& ctx)
{
    const ConsoleRegistry& cvars = Console->Registry();
    const auto read = [&cvars](const char* name, float fallback)
    {
        if (const CVarMetadata* c = cvars.FindCVar(name);
            c != nullptr && std::holds_alternative<double>(c->CurrentValue))
            return static_cast<float>(std::get<double>(c->CurrentValue));
        return fallback;
    };
    const float groundAccel = read("movement.ground_accel", 10.0f);
    const float airAccel = read("movement.air_accel", 10.0f);
    const float friction = read("movement.friction", 6.0f);
    const float stopSpeed = read("movement.stop_speed", 1.0f);
    const float maxAirSpeed = read("movement.max_air_speed", 1.0f);
    const float jumpSpeed = read("movement.jump_speed", 5.5f);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        World& world = reg->Components;
        if (!world.IsRegistered<MovementProfile>() || !world.IsRegistered<GameplayTagContainer>())
            continue;
        const MovementTags* ids = world.TryGetResource<MovementTags>();
        if (ids == nullptr)
            continue;

        world.ForEachComponent<MovementProfile>([&](EntityId entity, MovementProfile& profile)
        {
            const GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(entity);
            if (tags == nullptr || !tags->HasExact(ids->Controlled))
                return;
            profile.GroundAcceleration = groundAccel;
            profile.AirAcceleration = airAccel;
            profile.Friction = friction;
            profile.StopSpeed = stopSpeed;
            profile.MaxAirSpeed = maxAirSpeed;
            profile.JumpSpeed = jumpSpeed;
        });
    }
}
