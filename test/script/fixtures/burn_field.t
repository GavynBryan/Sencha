trigger BurnField {
    fn on_enter(ctx: TriggerContext) {
        if !ctx.other.has_tag(tag"character.player") { return }
        ctx.other.add_tag(tag"damage.burning")
    }

    fn on_exit(ctx: TriggerContext) {
        ctx.other.remove_tag(tag"damage.burning")
    }
}
