import "lib_motion.t"

behavior Pickup {
    fn spawn(ctx: BehaviorContext) {
        ctx.entity.BobMotion.base_height = ctx.entity.Transform.position.y
    }

    fn fixed(ctx: BehaviorContext) {
        let e = ctx.entity
        let t = f32(ctx.tick) * ctx.dt
        e.Transform.position.y = e.BobMotion.base_height
            + bob_offset(t, e.BobMotion.frequency, e.BobMotion.amplitude)
    }
}
