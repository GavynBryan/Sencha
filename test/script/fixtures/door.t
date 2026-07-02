component DoorState {
    open: bool = false
    locked: bool = true
}

interaction OpenDoor {
    fn can_interact(ctx: InteractionContext) -> bool {
        return ctx.target.has(DoorState) && !ctx.target.DoorState.locked
    }

    fn interact(ctx: InteractionContext) {
        let door = ctx.target
        if door.DoorState.open { return }
        door.DoorState.open = true
        door.add_tag(tag"door.open")
        ctx.cue(cue"door.open")
    }
}
