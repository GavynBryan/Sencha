component HookshotState {
    target: Vec3
    target_entity: Entity
    pulling: bool = false
}

ability Hookshot {
    param range: f32 = 1800.0
    param pull_speed: f32 = 2600.0
    param stop_distance: f32 = 80.0

    state Pulling

    fn start(ctx: AbilityContext) {
        let owner = ctx.owner
        let hit = ctx.physics.raycast(ctx.aim_origin, ctx.aim_direction, range, QueryMask.Hookshot)

        if !hit.valid || !hit.entity.has_tag(tag"hookshot.anchor") {
            ctx.cancel()
            return
        }

        ctx.commands.add(owner, HookshotState { target: hit.position, target_entity: hit.entity })
        owner.add_tag(tag"state.hookshot.active")
        ctx.cue(cue"hookshot.fire")
        enter Pulling
    }

    fn Pulling.fixed(ctx: AbilityContext) {
        let owner = ctx.owner
        let target = owner.HookshotState.target

        ctx.movement.pull_toward(target, pull_speed, stop_distance)

        if distance(owner.Transform.position, target) <= stop_distance {
            owner.remove_tag(tag"state.hookshot.active")
            ctx.commands.remove(owner, HookshotState)
            ctx.finish()
        }
    }

    fn cancel(ctx: AbilityContext) {
        ctx.owner.remove_tag(tag"state.hookshot.active")
        ctx.commands.remove(ctx.owner, HookshotState)
    }
}
