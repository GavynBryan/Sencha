# The Ability Workbench

Kyusu's Abilities workspace (`editor/ability_editor/`) authors `ability.definition`
data assets: when an ability may start, the phases it runs, the ops each phase
runs and where it goes next. Abilities are ordinary data documents in the
session's `DataDocumentStore`, so the Data workspace and the Workbench showing
one file edit one document, and every edit is a step of the shared journal.
The runtime it authors for is described in `docs/gameplay/abilitykit.md`; the
plan the Workbench is built to is `docs/plans/abilitykit-2.md` §9.

## Two libraries

- `ability_authoring` is GUI-free and is what the editor tests link: the element
  model, the edits, the checks, the recipes, the explorers' outlines and the
  `AbilityWorkbench` model that holds the open ability and its selection.
- `ability_editor` is `AbilityWorkspace`, the `IWorkspace` Kyusu builds from its
  kinds table. It composes the model with the shell's facets and owns nothing
  about abilities itself.

## Elements, keys and paths

Every element an author makes -- a condition, a local, a cost group, a phase, an
op, a presentation entry, a transition, a guard -- carries a stable key
(`DataFieldKind::StableKey`). `AbilityElementsOf` walks the data object and names
each element by key, with the JSON path it is at now and the phase it is in.
Selection, undo and problems name an element by key; a path is only where it is
until the next edit, so a selection follows its element through reordering, and
let go of when an undo takes the element away.

## Edits

`AbilityDocumentEdits` are pure functions over the data object, addressed by key:
add, move and delete phases (a deleted phase's incoming transitions end the
ability instead, after asking); insert and move ops between a phase's lists
(`enter`, the repeating `while` list, `hold`, `exit`) and between phases; add
transitions and locals; set or clear a guard; duplicate any listed element, every
key inside the copy minted afresh. `AbilityWorkbench::Edit` applies one to a
copy of the document and hands it to the document set as one step, so undo and
redo work the way every other data document's do. A refused edit is no step.

## Checks and problems

`CheckAbilityData` compiles the working data (`CompileAbilityDeclaration`, which
needs no World) and, when it compiles, resolves it against the names the project
declares: a `VocabularyCatalog` holding the engine's verbs, leases and queries,
the loaded game module's, and the tags, attributes, effects and abilities content
declares. The catalog is rebuilt on "Check Against Current Content". Checks are
cached against the document's revision.

`AbilityProblems` turns each diagnostic into a `Problem` named by its element
("Fire › effect.aply: no verb 'effect.aply'") and located by its path. Revealing
one opens the ability, selects the element the path is in, and brings the field
forward: the card's `Focus` names the property, and the shell's sheet opens the
groups holding it, marks its row and scrolls to it.

## The card

The card sits beside the strip as the inspector; the strip is the work. With an
element selected, the card is that element alone: `DataProperties`
scoped to the element's path, so its fields are edited by their whole paths
exactly as the whole document's are. An op's card adds its **arguments**
(`AbilityArgumentProperties`), laid out from the contract the catalog declares
for the verb, lease or query it runs (`AbilityOpContract`): each argument comes
from a constant, a port or a local (a hit's fields among the locals), as its kind
allows, and an optional one may be unset. An argument naming an effect offers
**New effect…**: the effect is made where the person says, the argument names it,
and the effect's sheet shows while the ability stays open on the strip -- picking
on the strip brings the card back to the ability.

Under the element are its **problems** (`AbilityProblemProperties`). A problem a
guard answers -- a hit that may not have struck, a recipient that may be gone or
lack what its argument needs -- offers that guard as one step
(`AbilityGuardFixFor`): `hit`, or `entity … has` with the component the
argument's contract names, joined to any guard the op already has.

With nothing selected, the card is the ability's **overview**
(`AbilityOverviewProperties`) -- the binding it gets with nothing overriding it
(`EffectiveAbilityBinding`, the merge the runtime's grant entries use), when it
may start, continue and is cancelled, its costs, cooldown and prediction, and its
problem count -- above the document's sheet. A new ability starts from a recipe
(`AbilityRecipes`): Blank, Instant, Held / Periodic, Charge / Release, Timed
Movement and Sustained Toggle, each an ordinary document whose keys are minted
when it is made.

## The phase strip

The open ability fills the workspace's content as a strip of cards, one per phase
in order (`AbilityPhaseStrip`, `editor/ui/ability_strip.rml`, on the window's
interactive surface). A card shows the phase's clock and how many transitions
come into it, its four op lists (on entry, repeating, held, on exit) and where it
goes next; an op shows its kind and what it runs, and a guarded op or transition
shows its guard as it reads (`AbilityConditionText`: "Hit struck", "phase ticks
≥ 6"). A lamp marks the worst problem on a row and on the card holding it.

What the strip shows is built GUI-free (`AbilityStripOf`) as flat row lists the
document repeats over, each row naming what holds it, so the presenter only
publishes rows and hands back what was asked for. Picking a card's head or a row
selects its element. A card or an op is dragged onto another to move it
(`DropAbilityElement`): a phase goes before the phase it is dropped on, an op
before the op, to the end of a list dropped on its heading, or to the end of a
phase's entry list dropped on its card. The innermost target hears the drop
first and takes it; a focus loss or a hidden tab lets go of a drag.

## Adding ops

**Operations** (O) is the palette: every verb, lease and query the project's names
declare, grouped, each with its scope and where it may be predicted, and the
kit's own ops (set a local, commit costs, cue). Choosing one inserts the op it
makes (`AbilityPaletteOp`) where `AbilityOpPlacementFor` says: the list whose "+"
opened the palette, else just after the selected op, else at the end of the
selected phase's entry list. A lease is only ever held, and nothing but a lease
is: a lease goes to its phase's held list, and anything else asked for there
goes to its entry list. A new op's arguments start empty, and the checks name
what it still needs.

## Preview

The open ability runs on a fixture character in a private World
(`AbilityPreviewSession`) on the shared preview clock
(`docs/editor/fixed-tick-preview.md`): the systems a game registers for a
character that acts and moves -- physics and the character mover
(`RegisterPhysics`), the kit (actor time, the ability pass, attribute resolve,
effect lifetime) and movement (`RegisterMovementSystems`) -- with the engine's
own operations bound exactly as the runtime binds them
(`EngineVocabularyBindings`), the project's content vocabulary, and a floor. The ability is its working data, compiled and resolved into the
preview World over whatever its saved file declared, so an edit replays the
preview to the tick it was on. The fixture is a capsule character in the free
locomotion mode holding a grant of it, every declared attribute at its default,
an input source, and an animation request set (it takes requests; no rig plays
them), so a movement lease moves it as it would a game's pawn.

The **stage** fills the content behind the strip (`AbilityStageRenderFeature` in
a `ContentPicture`): the preview drawn as lines up to the shown tick
(`AbilityStageOf`) -- the floor, the fixture's capsule, its trail, its aim, the
targets placed for it, and the points and normals its hit locals hold. It is
framed when an ability opens and on Frame; a drag on it orbits and the wheel
zooms.

The card's **Preview** section is the controls, each applied on the next tick:
press, release, tap (once or three times within the tick) or interrupt an input
the ability's binding names; set an attribute; grant or revoke a tag; scale the
fixture's time; place a target with every attribute along its aim. The controls
are a timed script: rewinding replays it from tick zero, so what a rewound tick
shows is what the systems produce, and acting at a past tick cuts what came
after it.

**This tick** (the card's details, `AbilityPreviewInspector`) reads the trace and
the records: each activation's phase, pulse, leases held, groups paid, cooldown
and end reason, and every event the pass traced -- starts and refusals with the
condition at fault, transitions, ops run and what they answered, skips, cues --
its elements named from the working data. The **deck** (`AbilityPreviewTimeline`)
shows each activation's stay in each phase as a span, the script's input and
every op that did not accept as marks, and one attribute plotted (Plot on its
row); the transport keys play, step, rewind and scrub it.

## Explorers

**Abilities** (A) lists the project's ability assets. **Elements** (E) lists the
open ability's conditions, locals and costs, then each phase with its ops (by the
list they run in) and transitions; choosing one selects it. **Operations** (O) is
the palette above.
