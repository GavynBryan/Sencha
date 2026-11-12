# Contact lines

A contact line is a straight line from an entity out to a tip, that strikes
characters touching it: a harpoon's chain, a whip, a tripwire thrown across a
gap. It is the thrower's, not a thing flying on its own. A projectile only finds
what the tip meets on the way out; the line owns the tip from then on.

| Piece | Owns |
|---|---|
| `ContactLine` (`line/ContactLine.h`) | how far it strikes, what a strike does, how fast it comes home, authored on a prefab root |
| `ContactLineSpan` | a mesh of that prefab stretched along the line each frame |
| `ContactLineState` | the authority's working state: tip, stage, what it has struck |
| `ContactLineSystem` (`line/ContactLineSystem.h`) | throwing, the stages, strikes, cuts, and the verbs and queries below |

## Authoring one

A line is two prefabs named by whatever throws it: a line prefab whose root
carries `ContactLine`, with whatever draws it beneath the root, and an ordinary
projectile prefab that carries no mesh. The root stands at the tip, facing out
along the line, so a barb is a child placed just behind the origin. A child with
`ContactLineSpan` is stretched from the tip back to the thrower: its mesh spans
`mesh_length` metres along +Z, centred.

| Field | Meaning |
|---|---|
| `radius` | metres around the line that strike a character |
| `effect`, `kind` | the effect a strike applies, and the tag its `projectile.hit` carries |
| `ignores` | a tag whose carriers it never strikes |
| `anchor_seconds` | how long the tip stays stuck before it comes away by itself |
| `retract_speed` | how fast the tip comes home; zero ends the line at once |
| `anchor_cue`, `cut_cue` | cues appended to the thrower's log where it sticks and where it is cut |

## Stages

| Stage | The tip | The line strikes |
|---|---|---|
| Extending | follows the projectile | everything but the last metre behind the tip |
| Anchored | stays where the projectile met level geometry | its whole length |
| Retracting | moves home at `retract_speed` | nothing |

The projectile's stop says which comes next (`ProjectileStops`, written by
`ProjectileSystem` each tick). Level geometry anchors the line: a body nothing
moves or hurts. Anything else, or running out of time, starts it home.

## One event per deployment

A character is struck at most once while a line is out, and never by both tip
and line. The two have no shared list; geometry does the work. In flight the line
stops a metre behind the tip, more than a tick's travel, so the tip reaches what
is ahead of it first and anything the line touches is already behind the tip. A
tip that strikes starts the line home the same tick, disarming it.

A discrete test is enough for the line. At 60 Hz it catches anything crossing it
slower than about 50 m/s; a faster crossing is missed, which favours the player.

## What ends one

Something solid standing across it for two ticks in a row cuts it, and while
anything is across it, it strikes nothing. `line.cut` ends every line an entity
has out, where it is. Its instigator ending ends it. Letting go of the lease
starts it home; nothing else depends on a line being let go.

## From content

| Name | Kind | Does |
|---|---|---|
| `line.extend {instigator, line, projectile, at?}` | lease, completes | throws a line, at a point or along the aim; complete once it has started home or ended |
| `line.cut {instigator}` | verb | ends every line the instigator has out |
| `line.retract {instigator, keep_newest?}` | verb | draws home every line it has left anchored, or all but the newest |
| `line.anchored {instigator}` | query | whether it is holding a line anchored |
| `line.anchored_within {instigator, point, distance}` | query | whether it is holding one anchored that close to a point |
| `line.anchor {instigator}` | query | where the line it is holding is anchored |
| `line.stuck {instigator}` | query | whether it has let go of a line and left it stuck |
| `line.stuck_point {instigator, short_of}` | query | a point on the newest such line, that many metres short of where it landed |

A throw is committed: letting go while it flies changes nothing until it stops,
when it stays if `keep` was given and comes home if not. `line.cut` ends a throw
in the air along with its line, so no tip goes on without one. Something solid
across the line cuts it only once it is out, since in flight the tip has just
come that way.

`max_length` ends the throw at that distance. `keep` on `line.extend` leaves an
anchored line in place when the lease is let go: nothing holds it now, it stays
until `anchor_seconds` runs out or `line.retract` draws it home, and `leash_slack`
holds its instigator within that many metres of where it stood when the line
anchored, through its tether slot, which any ability's tether takes first. With
two kept lines the tighter one holds. `rehit_seconds` strikes a character again
after that long off the line. `slots` is how many lines an instigator can have
out, spaced `slot_spacing` across its aim; a throw with every slot taken draws
home its oldest kept line, and is refused if none is kept.

A phase can hold `line.extend` for the whole activation with `"scope":
"activation"`, so a line can stay out while later phases reel in on it.

## Not yet

A line lives on the authority alone: it is not replicated, so a client draws
none (`docs/deferred.md`). It cannot wrap round a corner, and it cannot be shot.
