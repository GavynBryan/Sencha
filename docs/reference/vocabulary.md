# Authored vocabulary: `engine`

Generated from the World's catalog by `RenderVocabularyReference` (`authored/VocabularyReference.h`); change a declaration, not this file.
An argument reads `key: type = default [minimum .. maximum] (units)`; `key?` may be left out and has no default.

## Verbs

| Verb | Kind | Scope / prediction | Arguments | Result | Refusals | Description |
|---|---|---|---|---|---|---|
| `ability.activate` | instant | gameplay / authority_only | `target: entity`<br>`ability: data asset (ability.definition)`<br>`source?: entity` | — | — | Starts an ability the actor is granted, if admission allows, in the next ability pass; with a source, only the grant that source supplied. |
| `ability.cancel` | instant | gameplay / authority_only | `target: entity`<br>`ability: data asset (ability.definition)` | — | — | Ends the actor's running activations of an ability. |
| `ability.grant` | instant | gameplay / authority_only | `target: entity`<br>`ability: data asset (ability.definition)`<br>`key: string`<br>`source?: entity` | — | — | Lets the actor run an ability, under a key. |
| `ability.revoke` | instant | gameplay / authority_only | `target: entity`<br>`ability: data asset (ability.definition)`<br>`key: string` | — | — | Takes back an ability granted under a key. |
| `anim.cancel` | instant | gameplay / simulation | `target: entity`<br>`intent: tag` | — | — | Ends the held request this producer asked the entity for. |
| `anim.hold` | lease | gameplay / simulation | `target: entity`<br>`intent: tag` | — | — | Keeps an animated entity presenting an intent until its holder lets go. |
| `anim.present` | instant | cosmetic / presentation | `target: entity`<br>`intent: tag`<br>`lifetime: choice(impulse, fixed) = "impulse"`<br>`ticks: int = 0` | — | — | Plays an intent on the entity's presentation layers, on this machine alone. |
| `anim.request` | instant | gameplay / simulation | `target: entity`<br>`intent: tag`<br>`lifetime: choice(held, fixed, impulse) = "held"`<br>`ticks: int = 0` | — | — | Asks an animated entity to present an intent its rig declares. |
| `application.quit` | instant | gameplay / authority_only | — | — | — | Asks the host to exit. The game's exit handler still sees the request. |
| `audio.loop` | lease | cosmetic / presentation | `clip: asset (Audio)`<br>`gain: float = 1 [0 .. 1]`<br>`bus: string = "Sfx"` | — | — | Plays a sound on a loop while held. |
| `audio.play` | instant | cosmetic / presentation | `clip: asset (Audio)`<br>`gain: float = 1 [0 .. 1]`<br>`bus: string = "Sfx"` | — | — | Plays a sound once. |
| `camera.fov_scale` | instant | cosmetic / presentation | `scale: float [0.25 .. 4]`<br>`attack: float = 0 [0 ..] (s)`<br>`hold: float = 0 [0 ..] (s)`<br>`release: float = 0 [0 ..] (s)`<br>`subject?: entity` | — | — | Narrows or widens the view for a moment. |
| `camera.fov_scale.hold` | lease | cosmetic / presentation | `scale: float [0.25 .. 4]`<br>`attack: float = 0 [0 ..] (s)`<br>`hold: float = 0 [0 ..] (s)`<br>`release: float = 0 [0 ..] (s)`<br>`subject?: entity` | — | — | Narrows or widens the view while held, then eases back. |
| `camera.shake` | instant | cosmetic / presentation | `profile: data asset (camera.shake)`<br>`scale: float = 1 [0 ..]`<br>`subject?: entity`<br>`point?: vector3`<br>`seed: int = 0` | — | — | Shakes the view once. |
| `camera.shake.hold` | lease | cosmetic / presentation | `profile: data asset (camera.shake)`<br>`scale: float = 1 [0 ..]`<br>`subject?: entity`<br>`point?: vector3`<br>`seed: int = 0` | — | — | Shakes the view while held, then lets it settle. |
| `claims.hold` | lease | gameplay / authority_only | `group: tag`<br>`claim: tag`<br>`count: int = 1`<br>`share: float = 0` | — | — | Holds a place in a group's claim while held: a role, a token, a signal. Refused when every place is taken. |
| `counter.add` | instant | gameplay / authority_only | `name: tag`<br>`amount: int = 1`<br>`holder?: entity` | — | — | Adds to a named number, kept by saves. Reaching zero unsets it. Values past a 32-bit integer are held at its bound. |
| `counter.set` | instant | gameplay / authority_only | `name: tag`<br>`value: int`<br>`holder?: entity` | — | — | Sets a named number, kept by saves. Zero unsets it. Values past a 32-bit integer are held at its bound. |
| `effect.apply` | instant | gameplay / authority_only | `target: entity`<br>`effect: data asset (effect.definition)`<br>`scale: float = 1`<br>`instigator?: entity`<br>`from?: vector3`<br>`keep?: bool` | — | — | Applies an effect to an entity's attributes. |
| `effect.hold` | lease | gameplay / authority_only | `target: entity`<br>`effect: data asset (effect.definition)`<br>`scale: float = 1`<br>`instigator?: entity`<br>`from?: vector3` | — | — | Keeps a lasting effect on an entity until its holder lets go. |
| `effect.remove` | instant | gameplay / authority_only | `target: entity`<br>`effect?: data asset (effect.definition)`<br>`kinds?: {all?: array of string, any?: array of string, none?: array of string}` | `int` | `nothing_named`<br>`both_named` | Ends an entity's lasting effects of one definition, or of the kinds a tag query matches. |
| `equipment.cycle` | instant | gameplay / authority_only | `wearer: entity`<br>`group: string` | — | `not_authority`<br>`missing`<br>`not_permitted`<br>`not_equippable`<br>`already_equipped`<br>`no_slot`<br>`not_accepted`<br>`locked`<br>`grant_room`<br>`tag_room`<br>`missing_attribute`<br>`not_maintained`<br>`undeclared`<br>`unmet` | Wears the next entry the group's first slot accepts, in container order. |
| `equipment.equip` | instant | gameplay / authority_only | `wearer: entity`<br>`entry: entity`<br>`slot?: string` | — | `not_authority`<br>`missing`<br>`not_permitted`<br>`not_equippable`<br>`already_equipped`<br>`no_slot`<br>`not_accepted`<br>`locked`<br>`grant_room`<br>`tag_room`<br>`missing_attribute`<br>`not_maintained`<br>`undeclared`<br>`unmet` | Wears an entry in the slots its item occupies. |
| `equipment.unequip` | instant | gameplay / authority_only | `wearer: entity`<br>`slot: string`<br>`force: bool = false` | — | `not_authority`<br>`missing`<br>`not_permitted`<br>`not_equippable`<br>`already_equipped`<br>`no_slot`<br>`not_accepted`<br>`locked`<br>`grant_room`<br>`tag_room`<br>`missing_attribute`<br>`not_maintained`<br>`undeclared`<br>`unmet` | Takes off what a slot holds. |
| `interact.use` | instant | gameplay / authority_only | `activator: entity`<br>`range: float [0 ..]`<br>`origin?: vector3`<br>`direction?: vector3` | — | — | Uses the one thing the activator points at: the first body its aim strikes within range, when that entity answers interact.use. Trigger volumes are passed through. Authority only. |
| `inventory.drop` | instant | gameplay / authority_only | `entry: entity`<br>`at?: vector3`<br>`force: bool = false`<br>`keep: bool = true` | — | `not_authority`<br>`missing`<br>`not_accepted`<br>`container_full`<br>`too_deep`<br>`not_permitted`<br>`withheld` | Takes an entry out of its container into a pickup, in the zone where it lands. |
| `inventory.give` | instant | gameplay / authority_only | `into: entity`<br>`definition: data asset (item.definition)`<br>`count: int = 1 [1 .. 4294967295]` | `entity` | `not_authority`<br>`missing`<br>`not_accepted`<br>`container_full`<br>`too_deep`<br>`not_permitted`<br>`withheld` | Puts units of an item into a container, all or none of them. |
| `inventory.take` | instant | gameplay / authority_only | `pickup: entity`<br>`into: entity` | `entity` | `not_authority`<br>`missing`<br>`not_accepted`<br>`container_full`<br>`too_deep`<br>`not_permitted`<br>`withheld` | Takes a pickup into a container; the pickup goes. |
| `inventory.transfer` | instant | gameplay / authority_only | `entry: entity`<br>`into: entity`<br>`count: int = 0 [0 .. 4294967295]`<br>`force: bool = false` | — | `not_authority`<br>`missing`<br>`not_accepted`<br>`container_full`<br>`too_deep`<br>`not_permitted`<br>`withheld` | Moves an entry, or part of a stack, into a container. |
| `inventory.use` | instant | gameplay / authority_only | `entry: entity`<br>`user?: entity` | — | `not_authority`<br>`missing`<br>`not_accepted`<br>`container_full`<br>`too_deep`<br>`not_permitted`<br>`withheld` | Runs the item's use, with the entry as its source, in the next ability pass. |
| `level.change` | instant | gameplay / authority_only | `level: string`<br>`memory: choice(remembered, authored)`<br>`bodies: choice(kept, fresh)`<br>`kind: choice(scene, world) = "scene"`<br>`arrival: string = ""`<br>`landmark: string = ""`<br>`carry_within?: entity` | — | `busy`<br>`not_cooked`<br>`not_authority`<br>`in_session`<br>`body_in_zone`<br>`no_landmark`<br>`not_carriable` | Replaces the loaded level with another, or the same one again, saying what it keeps. |
| `line.cut` | instant | gameplay / authority_only | `instigator: entity` | — | — | Ends every line the instigator has out, where it is, striking nothing more. |
| `line.extend` | lease, completes | gameplay / authority_only | `instigator: entity`<br>`line: asset (Scene)`<br>`projectile: asset (Scene)`<br>`at?: vector3`<br>`keep?: bool` | — | — | Throws a line with a projectile and keeps it out while held. Letting go draws it home. |
| `line.retract` | instant | gameplay / authority_only | `instigator: entity`<br>`keep_newest?: bool` | — | — | Draws home every line the instigator has left anchored. |
| `movement.burst` | lease, completes | gameplay / simulation | `target: entity`<br>`direction: vector3`<br>`speed: float = 10 [0 ..]`<br>`seconds: float = 0 [0 ..]`<br>`rises: bool = false` | — | — | Drives the character at a velocity while held, or for a time. |
| `movement.impulse` | instant | gameplay / authority_only | `target: entity`<br>`direction: vector3`<br>`speed: float = 1 [0 ..]`<br>`rise: float = 0 [0 ..]` | — | — | Adds a velocity to the character's, on top of whatever it was moving at: a shove. |
| `movement.launch` | instant | gameplay / authority_only | `target: entity`<br>`direction: vector3`<br>`speed: float = 10 [0 ..]`<br>`rise: float = 0 [0 ..]` | — | — | Sets the character moving at a velocity, whatever it was moving at; it keeps that momentum afterwards. |
| `movement.mode` | lease | gameplay / simulation | `target: entity`<br>`mode: string` | — | — | Puts the character in a locomotion mode while held. |
| `movement.tether` | lease, completes | gameplay / simulation | `target: entity`<br>`anchor?: entity`<br>`point?: vector3`<br>`length: float = 2 [0 ..]`<br>`speed: float = 12 [0 ..]`<br>`until?: vector3` | — | — | Pulls the character toward an anchor once it is farther away than the rope. |
| `navigation.approach` | instant | gameplay / authority_only | `target: entity`<br>`subject: entity`<br>`follow: bool = false`<br>`arrive_radius: float = 0 [0 ..]`<br>`speed: float = 1 [0 ..]` | — | — | Sends the agent to an entity, replacing any order it has. |
| `navigation.move_to` | instant | gameplay / authority_only | `target: entity`<br>`point: vector3`<br>`arrive_radius: float = 0 [0 ..]`<br>`speed: float = 1 [0 ..]` | — | — | Sends the agent to a point, replacing any order it has. |
| `navigation.stop` | instant | gameplay / authority_only | `target: entity` | — | — | Ends the agent's order; it stands where it is. |
| `perception.stimulus` | instant | gameplay / authority_only | `source?: entity`<br>`position?: vector3`<br>`radius: float`<br>`only?: entity` | — | — | Something hearing picks up: a shot, a footstep, a hit. Heard by every ear within its radius, or by one observer alone. |
| `presentation.attach` | lease | cosmetic / presentation | `target: entity`<br>`socket: string`<br>`mesh: asset (StaticMesh)`<br>`material: asset (Material)`<br>`position?: vector3`<br>`rotation?: vector3`<br>`cast_shadows: bool = true` | — | — | Shows a mesh at a socket of the target while held. |
| `presentation.light` | lease | cosmetic / presentation | `target: entity`<br>`socket: string = ""`<br>`position?: vector3`<br>`color: vector3`<br>`intensity: float [0 ..]`<br>`range: float [0 ..]`<br>`peak_scale: float = 1 [1 ..]`<br>`pulse_seconds: float = 0 [0 ..] (s)`<br>`attack: float = 0 [0 ..] (s)`<br>`release: float = 0 [0 ..] (s)`<br>`packs_first: bool = false` | — | — | Shows a point light on the target while held, then fades it out. |
| `projectile.launch` | instant | gameplay / authority_only | `instigator: entity`<br>`projectile: asset (Scene)`<br>`origin?: vector3`<br>`direction?: vector3`<br>`spread: float = 0 [0 ..]`<br>`speed_scale: float = 1 [0 ..]`<br>`magnitude: float = 0 [0 ..]`<br>`splash_scale: float = 1 [0 ..]` | — | — | Launches a projectile prefab from the instigator's aim, or from an origin along a direction. The authority flies it and decides what it strikes. |
| `responses.disable` | instant | gameplay / authority_only | `entity: entity`<br>`binding: string = ""` | — | — | Stops a binding responding until it is enabled. |
| `responses.enable` | instant | gameplay / authority_only | `entity: entity`<br>`binding: string = ""` | — | — | Lets a binding respond again. |
| `runtime.resume` | instant | gameplay / authority_only | — | — | — | Closes the application shell and resumes local play. |
| `save.copy` | instant | gameplay / authority_only | `from: string`<br>`to: string` | — | `busy`<br>`bad_slot`<br>`nothing_loaded`<br>`not_authority`<br>`in_session` | Writes one slot with what another holds, its backup included. |
| `save.delete` | instant | gameplay / authority_only | `slot: string` | — | `busy`<br>`bad_slot`<br>`nothing_loaded`<br>`not_authority`<br>`in_session` | Deletes a slot's save and its backup. |
| `save.restore` | instant | gameplay / authority_only | `slot: string = ""` | — | `busy`<br>`bad_slot`<br>`nothing_loaded`<br>`not_authority`<br>`in_session` | Loads the saved level again and puts back what the save kept. |
| `save.write` | instant | gameplay / authority_only | `slot: string = ""`<br>`arrival: string = ""` | — | `busy`<br>`bad_slot`<br>`nothing_loaded`<br>`not_authority`<br>`in_session` | Writes how the world differs from its authored content. |
| `tags.grant` | instant | gameplay / authority_only | `target: entity`<br>`tag: tag`<br>`stacks: int = 1 [1 .. 65535]` | — | — | Gives the target stacks of a tag it keeps and saves; an entity with no tags gains them. |
| `tags.revoke` | instant | gameplay / authority_only | `target: entity`<br>`tag: tag`<br>`stacks: int = 1 [1 .. 65535]` | — | — | Takes stacks of a tag the target keeps; never what an effect or ability holds. |
| `time.scale` | lease | gameplay / simulation | `target: entity`<br>`scale: float [0.01 .. 4]` | — | — | Runs an actor's time faster or slower while held: its movement, effects, phases and animation. |
| `trigger.interact` | instant | gameplay / authority_only | `activator: entity` | — | — | Interacts with every trigger volume the activator stands in. |

## Queries

| Query | Arguments | Result | Description |
|---|---|---|---|
| `claims.count` | `group: tag`<br>`claim: tag` | `int` | How many places in a group's claim are held. |
| `claims.held` | `holder: entity`<br>`group: tag`<br>`claim: tag` | `bool` | Whether the entity holds a place in a group's claim. |
| `counter.value` | `name: tag`<br>`holder?: entity` | `int` | A named number; zero when it was never set. |
| `equipment.in_slot` | `wearer: entity`<br>`slot: string` | `entity` | The entry a slot holds. |
| `equipment.sources` | `wearer: entity`<br>`tag: tag` | `array of entity` | Every entry worn whose item carries a tag. |
| `inventory.count` | `container: entity`<br>`definition?: data asset (item.definition)`<br>`tag?: tag`<br>`within: bool = false` | `int` | How many units a container holds, spent entries not counted. |
| `inventory.entry` | `container: entity`<br>`definition?: data asset (item.definition)`<br>`tag?: tag`<br>`within: bool = false`<br>`last: bool = false` | `entity` | Searches a container, in its order, for the first unspent entry of an item or carrying a tag, or the last with 'last'. Not the entry a give made: that is the give's result. |
| `inventory.property` | `entry: entity`<br>`property: tag` | `float` | The number an entry's item states for a property; unavailable when it states none, or a value that is not a number. |
| `inventory.total` | `container: entity`<br>`property: tag`<br>`definition?: data asset (item.definition)`<br>`tag?: tag`<br>`within: bool = false` | `float` | Over a container's matching unspent entries, the sum of each entry's count times the number its item states for a property; an item stating none adds nothing. |
| `line.anchor` | `instigator: entity` | `vector3` | Where the line the instigator is holding is anchored; unavailable when none is. |
| `line.anchored` | `instigator: entity` | `bool` | Whether the instigator is holding a line anchored. |
| `line.anchored_within` | `instigator: entity`<br>`point: vector3`<br>`distance: float` | `bool` | Whether the instigator is holding a line anchored within a distance of a point. |
| `line.stuck` | `instigator: entity` | `bool` | Whether the instigator has let go of a line and left it stuck where it landed. |
| `line.stuck_point` | `instigator: entity`<br>`short_of: float` | `vector3` | A point on the newest line the instigator has left stuck, some metres short of where it landed; unavailable when none is. |
| `navigation.crossing_anchor` | `agent: entity` | `vector3` | The point the crossing it is making by ability hangs from; unavailable when none. |
| `navigation.crossing_exit` | `agent: entity` | `vector3` | Where the agent's centre stands at the end of the crossing it is making by ability. |
| `navigation.crossing_release` | `agent: entity` | `vector3` | Where along its rope the agent lets go to drop onto the exit. |
| `navigation.crossing_start` | `agent: entity` | `vector3` | Where the agent's centre stands to begin the crossing it is making by ability. |
| `navigation.failure` | `agent: entity` | `choice(none, no_zone, zone_unavailable, profile_unavailable, start_off_navigation, destination_off_navigation, destination_in_other_zone, no_path, search_limit_reached, route_too_long, target_lost, stuck, traversal_failed, traversal_unsupported, taken_over_by_input)` | Why the agent's latest order failed; none while it has not. |
| `navigation.phase` | `agent: entity` | `choice(idle, planning, moving, traversing, holding, arrived, failed, cancelled)` | Where the agent's latest order stands. |
| `navigation.remaining` | `agent: entity` | `float` | Metres of route the agent still has to walk. |
| `perception.last_seen_position` | `observer: entity`<br>`subject: entity` | `vector3` | Where the observer last sensed the subject, not where it is now. |
| `perception.last_seen_velocity` | `observer: entity`<br>`subject: entity` | `vector3` | The subject's velocity as the observer last measured it; zero when it could not. |
| `perception.nearest_remembered_tagged` | `observer: entity`<br>`tag: tag` | `entity` | The closest remembered position of something alive carrying the tag. |
| `perception.nearest_seen_tagged` | `observer: entity`<br>`tag: tag` | `entity` | The closest thing carrying the tag that the observer saw this tick, by sight alone. |
| `perception.remembers` | `observer: entity`<br>`subject: entity` | `bool` | Whether the observer's memory holds the subject. |
| `perception.seconds_since_seen` | `observer: entity`<br>`subject: entity` | `float` | Seconds since the observer last sensed the subject. |
| `perception.seconds_tracked` | `observer: entity`<br>`subject: entity` | `float` | Seconds the observer has sensed the subject without a lapse. |
| `perception.sees` | `observer: entity`<br>`subject: entity` | `bool` | Whether the observer saw the subject this tick, by sight alone. Unavailable when its sight scan did not finish. |
| `perception.sees_tagged` | `observer: entity`<br>`tag: tag` | `bool` | Whether the observer saw anything carrying the tag, or a tag beneath it, this tick, by sight alone. Unavailable when its sight scan did not finish. |
| `physics.clear_path` | `from: vector3`<br>`to: vector3`<br>`radius: float`<br>`height: float`<br>`ignore?: entity` | `bool` | Whether a line, or an upright capsule centred on it, reaches the end without meeting anything solid. Characters and triggers do not count. |
| `physics.overlap` | `position: vector3`<br>`radius: float`<br>`ignore?: entity` | `array of entity` | Every entity whose body overlaps a sphere. |
| `physics.trace` | `origin: vector3`<br>`direction: vector3`<br>`range: float`<br>`ignore?: entity` | `{hit: bool, entity: entity, point: vector3, normal: vector3}` | The first body a ray strikes. |

## Events

| Event | Source | Payload | Instigator | Description |
|---|---|---|---|---|
| `counter.changed` | — | `name: tag`<br>`value: int`<br>`holder: entity` | — | A named number took a new value, zero when unset. |
| `interact.use` | — | `activator: entity` | activator | Someone used this: it was the first thing their aim struck, within their reach. |
| `level.arrived` | — | `instigator: entity` | — | Something a level change carried arrived at this landmark, once for each. |
| `projectile.hit` | — | `instigator: entity`<br>`projectile: entity`<br>`kind: tag`<br>`point: vector3`<br>`normal: vector3`<br>`region: tag` | instigator | A projectile struck this entity directly. Splash is not a strike. |
| `sensor.enter` | — | `activator: entity` | activator | Something entered the volume. |
| `sensor.exit` | — | `activator: entity` | activator | Something left the volume. |
| `sensor.interact` | — | `activator: entity` | activator | Something inside the volume asked to interact. |
| `sensor.stay` | — | `activator: entity` | activator | Something is inside the volume: every sample, its entry included. |
