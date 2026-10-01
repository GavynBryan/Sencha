# Animation workspace on the grammar

Status: **accepted on 2026-10-01**, with every decision below taken as proposed.
Not yet built; it is M13's design. It applies `docs/editor/interaction-grammar.md` to the Animation
workspace, whose 23 ImGui panels are the last large ImGui surface in `editor/`.
`docs/plans/animation-authoring.md` §2 still describes a docked layout with
Reset Animation Layout; the grammar has no docking and nothing to lay out, so
that section is to be rewritten to point here when M13 lands.

## What stays exactly as it is

Most of what the panels do already lives below them, toolkit-free, in
`editor/animation_editor/src/authoring/`:

- `AnimationPreviewWorkspace` (the composition root);
- the rig scenario, with its simulation session and navigation;
- the audition selection and clip session;
- the JSON edit helpers for flows, selectors, predicates, rigs and names;
- take comparison, the session lab, the scenario batch and the clip-player scan.

All documents go through the one journal (`DocumentSourceSet`), via data
document transactions, plus the clip events document's own
begin/preview/commit. This proposal touches none of that. It replaces only the
presentation, and it removes two last couplings:

- `authoring/` uses `ui/DataForm.h`'s `FieldEdit` and `ApplyFieldEdit`. Those
  move to a property source, as Data did in M11.
- The preview feature hands out an `ImTextureID` through `DisplayedTargets`.
  It moves to a host image, as Materials did in M10.

## The screen

```
┌─[≡][ ANIMATION ◉ ][ upper.rig ● ]──────────── notices ──────[⚠1][_ □ ×]┐
│                                                     [ORBIT · JOINTS · PATH]│  view
│                      the rig on its stage, full bleed                      │
│        joints as pickable marks · root path and walls drawn in the world   │  ┌ card ┐
│                                                                            │  │ bone │
│                                                                            │  └──────┘
│ [◆ RULES]                                        [▣ hero.rig · walk.scn]   │  stations
│ ═══ TAPE DECK ════════════════════════════════════════════════════════════ │
│ [AUDITION|SIM] ⏮ ▶ ⏭ ⟲ 1.0×  0042 / 0180 · 60 Hz   ▏▏▏ │ ◆ ◆   ● ◆         │  transport + ruler + marks
│ sections ▕▔▔▔ idle ▔▔▔▕▔▔ walk ▔▔▕▔▔▔▔▔ run ▔▔▔▔▕   decisions │  │    │      │  lanes (per lens)
└────────────────────────────────────────────────────────────────────────────┘
```

| Place | Animation's meaning |
|---|---|
| Content | `AnimationPreviewRenderFeature` publishing a host image through `ContentPicture`. Orbit is a drag, zoom is the wheel, F frames. Joints are marks you can pick (the way Level's light and camera markers are), and picking one gives the bone card. The root path and the walls are world strokes. |
| Bottom left (operation) | The **lens** station: Q and a click on the station choose among Rules, Layers, Flow, Blends, Events, Root motion, Requests and Facts. A lens decides what the card, the drawer and the deck's lanes show. It never changes the clock or the preview subject. |
| Bottom right (holding) | The **rig and scenario** cartridge, which is the preview subject. Clicking it opens the Rigs explorer. "New rig" is a creation card, as Project and Data do it, using the recipe presets. |
| Top right (view) | Overlays: joints, path, ghost of take A, plus mute and solo state. Frame and orbit reset are here too. |
| Bottom edge | The **tape deck**: one transport for both clocks, a ruler, marks, and lanes the lens chooses. |
| Right edge | The selection card, then the drawer: a bone, an event, a rule, a layer, a fact, a request. |
| Problems lamp | Load problems, undeclared names (with "Declare beside the rig" on the rig card they reveal to), content risk, unresolved bindings and validation. |
| Document dial | The rig's open data documents and the clip events documents, with dirty lamps. Save and Save All work as everywhere else. |

The authoring plan has three selections, and they map like this:
- The **preview subject** is the cartridge.
- The **authoring selection** is the card.
- The **inspection time** is the deck's playhead: live, or a recorded tick.

None of the three moves another. Auditioning content is an explicit action:
"Audition" on a content row or a slot row switches the deck to the audition
clock and says so on the deck.

## The tape deck

This is the one new shell instrument. It is presented from a new
`WorkspaceTimelineFacet`, which the grammar already reserves.

- **Clock:** the source (Audition or Simulation), time, range, rate, playing,
  speed, loop. Simulation adds the recorded range, which is what makes
  scrubbing back a history inspection (`Navigation.InspectRecord`), not a
  replay.
- **Actions:**
  - `timeline.play`, `timeline.restart`, `timeline.step_back`,
    `timeline.step_forward`, `timeline.loop`, `timeline.speed.*`,
    `timeline.source.*`, `timeline.live`.
  - Each is offered through `OfferAction`, so each is a chord, a console
    command and a hatch entry.
- **Seek:** a drag on the ruler scrubs and a release stays there. In
  simulation it inspects a recorded tick; dragging past the live tick runs to
  that tick.
- **Marks:** clip events (diamonds for gameplay, circles for cosmetic),
  request issue and cancel, behavior changes, corrections. A mark is
  selectable, and that gives its card. An event mark is draggable: retiming is
  one edit session on `AnimationClipEventsDocument`
  (begin/preview/commit/cancel), so every interruption cancels it.
- **Lanes:** the lens chooses at most two.
  - Flow sections, with the playhead and the cancel lane.
  - Per-layer winners and decisions.
  - Fact plots, root x/z/yaw curves, A/B residual, lab agreement.
  - A plot lane is a row of samples presented as bars, which is a generic
    presenter rather than an animation one.

## Lenses, and what each puts on the card and in the drawer

| Lens | Card (selection) | Drawer (everything) | Deck lanes | Replaces |
|---|---|---|---|---|
| Rules | A rule, with its verdict now, next tick or recorded. Links: Behavior, Used by. | The active selector document as a sheet, with rules, predicates, stay, and verbs to add, remove and move. | Decisions | Rules, Behavior, Slot map, Decisions |
| Layers | A layer: mode, weight and its source, mask summary, mute and solo. A **bone**: coverage per layer, add and remove mask steps. | The layer table and mask steps. The Skeleton explorer is the joint tree. | Winners per layer | Layers, Skeleton and masks |
| Flow | A section: loop, while/count, branches, ends. | The flow document sheet. | Sections (draggable) | Flow |
| Blends | The fade or inertialization in progress. | Recent blends and pairwise overrides. The blendspace pad is a purpose-built document: dragging sets the axis facts, as now. | Fade progress | Blends, Blendspace |
| Events | An event: name, time, scope, min weight, binding (choice, or "Create binding…"), inputs. | The clip's events, and admissions as authority or client. | Event marks | Clip events, Event admissions |
| Root motion | Move character, walls. | Walls editor; carried vs achieved. | Root curves | Root motion |
| Requests | A live request with Cancel. | Issue form, outcomes, the request schema sheet. | Request marks | Requests, Request schema authoring |
| Facts | A fact: now, next tick, input or derivation, set and clear. | All facts. | Plot of the selected fact | Facts |

Every document sheet is the M11 machinery: `DataProperties` plus a registered
`IDataSubtypeAuthoring` for each animation subtype that needs more than its
schema (selector predicates, flow structure, rig masks, request schema
budget). This is the same path input and movement took, so the animation
sheets and the Data workspace's sheet of the same asset are one
implementation.

## Explorers and benches

- **Explorers:**
  - Content (C): meshes, skeletons, clips, materials. Picking one sets the
    audition subject; Enter auditions it.
  - Rigs and scenarios (R).
  - Skeleton (J): the joint tree, which picks the bone card.
- **Benches:** the work there is a run, not a selection. A bench is a sheet,
  as Data's sheet is, and the deck shows its own clock while it is up.
  - Session lab: join, latency, loss, injections, the agreement strip.
  - Scenario batch: verdict table.
  - Clip-player migration.
  - Imported trace: file and filter.
  - A/B takes.
- **The Document panel goes.**
  - `NewDataAssetForm` is replaced by `DataCreation`, and `DataDocumentTabs`
    by the document dial.
  - After that, `DataForm`, `SchemaWidgets`, `DataDocumentTabs` and
    `NewDataAssetForm` have no users left and are deleted from
    `editor/common`. That clears the last data UI out of the ImGui allowlist.

## Decisions (accepted)

1. **One deck for both clocks?** The proposal is one deck with a source switch
   on it. The alternative is a deck per clock, which is clearer about which
   clock is running but takes twice the edge.
2. **The lens set.** Eight lenses, as above. Flow is a lens of its own because
   its sections are the deck's main lane. The grammar's list has seven and
   folds Flow into Layers.
3. **Plots.** Bars in deck lanes, a generic sample-strip presenter, instead of
   line plots in the drawer. Line plots need either a polyline primitive in
   Shoji (an engine addition) or a host-rendered image per plot. Bars need
   neither.
4. **Benches as sheets** that take over the content while they run, rather
   than drawers beside the stage.
5. **Chords.**
   - Explorers: C, R, J.
   - Deck: Space for play, comma and period to step, Home to restart, L for
     loop, and the bracket keys for speed.
   - All rebindable. C is also Level's edge cut, which is fine: since
     1c1dc0ee a chord shared across workspaces reaches the one in use.

## Stages (one commit each)

- **M13a:**
  - The stage as a host image, with orbit, zoom, frame and joint marks, and the
    bone card.
  - The cartridge, the Content, Rigs and Skeleton explorers, and the document
    dial.
  - Animation documents as sheets.
  - Delete the Document panel and the common data UI.
- **M13b:**
  - The timeline facet and the tape deck, with both clocks, history scrub,
    event marks with drag-retime, and the event card.
  - Delete Content timeline, Simulation, Clip events and the Decisions slider.
- **M13c:** the Rules lens with selector authoring. Delete Rules, Behavior,
  Slot map and Decisions.
- **M13d:**
  - The Layers, Flow, Blends (blendspace pad) and Root motion lenses, and plot
    lanes.
  - Delete the matching panels.
- **M13e:**
  - The Facts and Requests lenses, request schema, admissions, and problems
    with Declare.
  - Delete the matching panels.
- **M13f:** the benches (lab, batch, migration, trace, takes). After this the
  animation editor has no ImGui allowlist entries, which unblocks M14.
