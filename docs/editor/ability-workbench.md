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

With an element selected, the card is that element alone: `DataProperties`
scoped to the element's path, so its fields are edited by their whole paths
exactly as the whole document's are. With none selected it is the document's
sheet. A new ability starts from a recipe (`AbilityRecipes`): Blank, Instant,
Held / Periodic, Charge / Release and Sustained Toggle, each an ordinary document
whose keys are minted when it is made.

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

## Explorers

**Abilities** (A) lists the project's ability assets. **Elements** (E) lists the
open ability's conditions, locals and costs, then each phase with its ops (by the
list they run in) and transitions; choosing one selects it. **Operations** (O) is
the palette above.
