import "lib_motion.t"

// Bobs an entity's height around an authored base with a deterministic sine.
// Iterates the native Transform plus the script BobMotion; the base height is
// authored data (a system has no one-time capture, unlike the old spawn latch).
system Bob {
    over Transform, BobMotion
    writes Transform
    fn fixed(ctx: FixedContext) {
        let e = ctx.entity
        let t = f32(ctx.tick) * ctx.dt
        e.Transform.position.y = e.BobMotion.base_height
            + bob_offset(t, e.BobMotion.frequency, e.BobMotion.amplitude)
    }
}
