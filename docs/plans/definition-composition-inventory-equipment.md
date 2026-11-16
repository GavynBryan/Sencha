# Definition composition, inventory and equipment

Status: **revision 3, approved (2026-10-15). Investigation complete.** D1–D12
are approved; D3 closed after the Stage 0 benchmarks, with conditions (§H). Implementation proceeds
stage by stage under separate plans. Stage 0's plan is
`docs/plans/composition-stage0-prerequisites.md`. Written against main
93476b54.

**On main:** Stages 0 and 1 were ported from `claude/composition` onto a later
main as module ABI v48 and net protocol 9. ABI and protocol numbers below are
the branch's own.

- Owner rulings on revision 1:
  - D1 approved in principle.
  - D2, keyed merge with a versioned format change, approved with compatibility tests.
  - D3, entries as entities, approved provisionally, subject to the memory and
    replication benchmarks in Stage 0.
- Revision 2 answers the owner's eight review points. The dispositions are in
  §R, and each points to the sections it changed.
- Owner rulings on revision 2:
  - D4, D6, D7, D8 and D10 approved.
  - D5 approved in principle; the wire and ABI gates and tests stay.
  - D9 approved, conditional on auditing every tag producer and consumer
    (§B.4).
  - D11 approved in principle, with the split between item quantity and
    expendable resources still to be worked out.
  - D12 rejected as written.
- Revision 3 answers those rulings, in §R.2. Only the affected sections
  changed.
- **Stage 0 and Stages 1–2 are ready for implementation planning.** They are
  reviewed separately from inventory and equipment.

Audience: the owner, and whoever implements an approved stage.

File references are to main at the commit above. "Read" means confirmed by
reading source. "Inferred" means not confirmed.

---

## R. Review dispositions (revision 2)

| # | Point | Disposition | Where |
|---|---|---|---|
| 1 | Grant identity, duplicate grants | **Accepted; revision 1 was wrong.** Collapsing to the lowest grant id is withdrawn. Entry identity becomes content-qualified on the wire, fixing a latent mismatch that exists today. Source selection uses AbilityKit's existing explicit binding priority, supplied per slot. Aggregation belongs to effects, not grants. A source that has not arrived resolves later through a generic deferred-reference rule. | §B.2, §D.5, §D.12, D5 |
| 2 | Atomic ability costs | **Accepted, narrower.** No transactional verb framework. Instead, a registered *cost provider* table extends the commit's existing check-all-then-apply-all. Providers get batch checks, infallible applies, and check-only prediction. | §B.3, D10 |
| 3 | Derived-state reconstruction | **Accepted, and it found a concrete defect.** The tag serializer persists contributed stacks, so restore followed by reconcile would double them. Reconstruction is split: authority reconciles gameplay; clients derive presentation only. Reconstruction is a reconcile, never "apply again". | §B.4, D9 |
| 4 | Effect lifetime, equip validity | **Accepted, and it found two concrete defects.** The pass grants before it revokes, and a full grant table fails silently. Equip effects must be maintained (Infinite). One-shot actions are a separate list, added only where a known consumer exists. The equip check covers every capacity. | §B.5, §D.4 |
| 5 | Conflict resolution | **Accepted with one holdout.** Runtime precedence is legitimate. Explicit `precedence` on Override modifiers, and slot `order` for presentation. Static composition conflicts stay errors, because an explicit local value is the explicit precedence and include order is not. | §B.6, D7 |
| 6 | Dropped-item persistence | **Accepted.** Every entry has persistent identity from creation, through the deferred runtime mint whose stated trigger this is. References are persistable. Ownership moves go through one function. Revision 1's `InitialItems` is withdrawn: it would duplicate items on zone re-import. | §B.7, §D.2, §D.9, D8 |
| 7 | Multi-slot and consumable lifecycles | **Accepted.** One record per logical equip. Count 0 means *depleted*, not destroyed, so the commit never destroys a live source. A reaper removes depleted entries once nothing equips or runs from them. | §B.8, §D.11 |
| 8 | Performance | **Accepted, and it corrects revision 1.** Replicated entries are *not* free per tick: replication walks every replicated entity on every publish. Entries replicate only while some peer is relevant to their container. The Stage 0 benchmarks measure entities, not payloads. No caching until measured. | §C.3, §F, Stage 0 |

**What is unchanged:**
- no prototype framework, item scripting, property bag, or component
  reference counting;
- compile-time composition shared by prefabs and data assets;
- AbilityKit owns runtime contributions;
- a small inventory and an equipment relation.

**Composition stands alone.** Stages 1–2 ship and are accepted on non-item
consumers (prefab variants, ability and movement-profile variants) before any
inventory code exists (§G).

### R.2 Revision 3 dispositions

| Ruling | Resolution | Where |
|---|---|---|
| **D9 audit** | Every writer to `GameplayTagContainer` is classified as baseline or contribution. Only authored load, editor edits and an explicit gameplay "persistent fact" call write baseline. Replication decode writes only contributions, so a received effective count can never become saved baseline. | §B.4 |
| **D11 quantity versus resources** | Three distinct things. **Quantity** (`ItemEntry.Count`, interchangeable units) reaches 0 only transiently and is always reaped. **Charges** are an instance's expendable resource: an attribute on the entry, and the item persists at 0. **Contents** are entries inside an item acting as a container; a Zelda bottle is one bottle that holds or doesn't hold a potion. Slot policy becomes `release` or `hold` (keep the assignment with no source, and rebind on refill). Unassigned depleted entries are reaped as soon as nothing runs from them. | §B.8, §D.11 |
| **D12 client materialization** | Rejected design withdrawn. A component type may declare itself **authority-materialized**. On a non-authoritative machine, content imports (level, zone, net prefab) skip entities that carry such a component, and the authority never sends their authored identity. They reach clients only as ordinary replicated spawns while relevant, and leave through the existing per-peer destroy channel. **The snapshot applier is not changed**, and its refusal to create a missing authored entity stands. | §B.7, D12 |

---

## 0. The answer

**Yes, Sencha can capture what is valuable in Thief's archetypes and
metaproperties, without a prototype system.**

Thief uses one runtime inheritance graph for three jobs, which Sencha keeps
separate:

| Thief job | What it is | Sencha owner |
|---|---|---|
| Archetype → concrete defaults; static metaproperties on archetypes | Static composition of reusable definitions | **Compile-time composition** of prefabs and data assets. Prefabs already compose nested placements with sparse patch/add/remove/suppress records, flattened at cook. They need `extends` without a wrapper, keyed collection merge, and fragments. |
| Metaproperties added and removed at runtime | Reversible, source-owned contributions to a live entity | **AbilityKit effects** (modifiers, tags, ability grants, owned by a `LeaseOwner`) plus leases, with narrow extensions (§B.2–B.5). |
| `Contains`, `StackCount`, `CombineType`, `InvType` | Possession | A small **inventory** mechanism. None exists. |

**Equipment** is a **relation** from receiver slots to an item entry. Its
contributions are derived:
- one set of maintained effects per equipped item, owned by that item;
- presentation computed from the relation.

> **Persist and replicate relations; derive contributions.** Possession and
> equipment are authoritative facts. Effects, grants, tags and presentation are
> consequences. The authority reconciles them from the facts; clients derive
> only presentation; the source withdraws them exactly.

**Build:**
- one core composition operator;
- `extends`/`includes` on prefabs and data assets;
- AbilityKit extensions:
  - grant source and content-qualified entry identity;
  - grant parameters;
  - cost providers;
  - grant reservation;
  - transient request grants;
- a persistable entity reference;
- the runtime persistent-id mint;
- inventory entries;
- an equipment relation;
- a shared sustained-presentation mechanism with a few cosmetic leases.

**Do not build:**
- a prototype asset kind;
- runtime inheritance;
- per-source component reference counting;
- item scripting;
- an equipment class hierarchy;
- a transactional verb framework.

---

## 1. Terms

| Term | Meaning | Not to be confused with |
|---|---|---|
| **ECS archetype** | Runtime component signature and its 16 KB chunks. | Anything authored. |
| **Definition** | Any authored, reusable, runtime-immutable description. Entity-shaped definitions are **prefabs** (`.sscene`). Everything else is a **data asset** (`.sdata`). | A live entity. |
| **Base** | The one definition a definition `extends`. | A C++ base class. |
| **Fragment** | A partial definition merged *into* another by `includes`: a single-entity scene, or a partial `data` object. The RFC's "feature"; Thief's static metaproperty. | A nested placement, which adds entities. |
| **Composition** | Base → fragments → local overrides, resolved before runtime: at cook for prefabs, at data-asset compile for data assets. | Contribution. |
| **Contribution** | A reversible runtime change to a live entity, owned by a **source**: an `ActiveEffect`, a lease, or a contribution entity. | Composition. |
| **Maintained contribution** | A contribution that holds only while its owner holds it: an Infinite effect's Current-folded modifiers, its tags, its grants; a lease. | A **one-shot action**: an Instant effect, or a periodic effect's Base change, which is a consequence and is never withdrawn. |
| **Baseline state** | What an entity has independent of contributions: authored components, attribute `Base`, authored grants, authored tag stacks. Persisted. | **Contributed state**, which is reconciled and never persisted. |
| **Source** | The entity that owns a contribution: `LeaseOwner.Entity`. | `VerbInvocation::Instigator`. |
| **Item definition** | An `item.definition` data asset. | An entity. |
| **Item entry** | One possession: a stack (definition + quantity) or a unique instance (quantity 1 plus instance components). One entity per entry, never per unit. | A pickup. |
| **Quantity** | `ItemEntry.Count`: how many interchangeable units the entry is. Quantity 0 is **depleted**, a transient state that exists only until nothing runs from the entry. | **Charges**: an instance's expendable resource (oil, wand charges, sips), an attribute on the entry. The item persists at 0. **Contents**: entries held *by* an item that is itself a container (a bottle's potion). |
| **Authority-materialized** | A component type's declaration that entities carrying it are created only by the authority. Clients hold them only as replicas. | Content-materialized (the default): every machine imports the entity from content and binds it by persistent id. |
| **Pickup** | A world entity that yields an entry when taken. | An entry. |
| **Container** | An entity holding entries. | A UI grid. |
| **Equipment record** | One logical equip on a receiver: definition, source, the set of slots it occupies, and one set of maintained contributions. | A slot. |
| **Equipment slot** | A gameplay position authored in an `equipment.layout`. | A **socket**: a presentation joint. |
| **Persistable reference** | An entity reference field that is an `EntityId` at runtime and a `PersistentEntityId` in scenes and saves. | A raw `EntityId` field, which has no scene codec today. |

---

## 2. What Thief does, and what we keep

Evidence comes from three places: openDarkEngine at `7a2d7baa` (unfinished, no
gameplay code), the reverse-engineered LG headers in NVScript, and the Dromesday
Book.

- **One relation builds the tree.** `MetaProp` links serve both purposes:
  priority 0 is the archetype, priority ≥ 1024 is a metaproperty (opde
  `InheritService.cpp:161-171,296-302`).
- **A read resolves a *donor*; nothing is merged.** Values are stored sparsely
  per property (`std::map<int,T>`). A read finds the donor object and takes its
  value. Precedence:
  1. the object's own value;
  2. the highest-priority metaproperty;
  3. the archetype chain.
- **Donor caches.** opde keeps a dense donor array per property. The original
  engine had a global tunable donor cache with DromEd stats commands.
- **Merge policy varies by property, implicitly.** Most properties override.
  `Scripts` and `Receptrons` accumulate. `Sources` cannot be masked. Some
  properties never inherit, or inherit only to abstract objects.
- **Propagation is partial.** opde refreshes descendants when *links* change,
  but not when a *field* on an archetype changes (a `valueChanged` TODO).
  Archetypes are assumed to be static during play.
- **Runtime metaproperties** are added and removed by scripts, stim responses
  and AI. In opde the newest gets the highest priority. The original default
  ordering is unconfirmed.
- **Inventory is `Contains` links** with a contain type. Items with the same
  `CombineType` merge into a stack ("differences … are lost"). Every possession
  is a full object. T1 and T2 have no worn slots; SS2 encodes slots as
  contain type `slot + 1000`.
- **Designers' pain points** (Dromesday):
  - "behaving strangely … because of some property it's inheriting";
  - tools needed just to tell direct values from inherited ones;
  - local values silently defeating metaproperties;
  - lost objects and missing archetypes.

**Keep:**
- definitions built by naming a parent plus reusable bundles;
- the override-versus-accumulate distinction;
- runtime attach and detach of capability bundles;
- classification ("is it a Guard?").

**Drop:**
- runtime donor resolution and its caches;
- implicit per-property merge policy;
- implicit priority;
- one full object per possession;
- archetype edits during play being undefined.

**Instead:**
- the schema declares the merge policy;
- static conflicts fail at the field;
- runtime precedence is explicit and inspectable (§B.6);
- classification is a contributed hierarchical gameplay tag.

---

## A. Existing architecture assessment

### A.1 Reusable as is

| Mechanism | Evidence | Use here |
|---|---|---|
| Prefab composition: nested placements, sparse `patch`/`add`/`remove`/`add_entities`/`suppress`, cycle detection, inner-to-outer application, dangling-override diagnostics | `editor/common/src/scene_source/SceneComposition.h:10-35`, `.cpp:52-68,150-323`; `SceneSourceDocument.h:58-107`; `test/editor/SceneCompositionTests.cpp:140,189` | Substrate for `extends`/`includes`. |
| Cook-time flattening; `.smap` value trees; `SceneCache` | `DocumentCookInput.cpp:305-389`; `SmapFormat.h:20-101`; `SceneCache.h:200-208` | Composed prefabs cost nothing extra at runtime. |
| Data assets: `DataSchema` (`StableKey`), compile at load, `Dependencies`, in-place reload with `ReloadVersion` | `core/metadata/DataSchema.h:14-167`; `DataAssetLoader.cpp:49-170`; `DataAssetCache.cpp:39-57` | Item definitions, container rules, equipment layouts. |
| Effects as entities: `LeaseOwner` ownership, `RemoveEffectsOwnedBy`, exact tag revocation, canonical fold, effect-held grants | `effects/ActiveEffect.h:19-34`; `EffectSystem.h:15-24`; `abilitykit.md` "Effects", "Grants and input" | Equipment contributions; "an equipped weapon is an Infinite effect granting its abilities" (`abilitykit-2.md:180`). |
| Counted tags | `GameplayTagContainer.h:88-120` | One ring's removal keeps another ring's tag. |
| Grant ids: per actor, never reused; revocation ends only that grant's activations | `AbilityGrants.h:20-51` | Unequipping one source never touches another's activations. |
| Explicit binding priority with diagnosed ties (`BindingAmbiguous`) | `AbilitySystem.cpp:65-154` | Source selection between equipped duplicates (§B.2), and the precedence model §B.6 generalizes. |
| Commit as check-all then apply-all | `AbilityCommit.cpp:18-91` | The transaction cost providers join (§B.3). |
| Leases with liveness-bounded owners | `abilitykit-2.md` §4; `HeldMotion.h`; `ActorTimeLeases.h` | Wing Cap flight with no component churn. |
| Authored verbs, queries, events with codegen | `docs/gameplay/authored-api.md`; `templates/arena/src/ArenaScore.h:65` | `inventory.*`, `equipment.*`. |
| Sustained cue presentation as a held/desired lease diff | `AbilityPresentationSystem.cpp:137-158,231-239` | The pattern for equipment and status presentation. |
| Tag-driven animation predicates and slot-map rows ("the stance-change and weapon-swap case") | `animation.md:272-297,365-376` | Wield poses with no new animation machinery. |
| `SectionMask`, per-slot material sets | `StaticMeshComponent.h:20-77`; `SkinnedMeshComponent.h:43-65` | Modular bodies. |
| `PersistentEntityId`, `PersistentEntityIndex`, `ZoneStateStore` | `world/identity/*`; `zone/ZoneStateStore.h`; `RuntimeWorld.cpp:340-371` | Entry identity. Authored entries taken out of a zone are already suppressed on re-import (§D.9). |
| Game payload messages with peer-verified `From` | `NetMessageRouter.h`; `TurretControl.cpp:205-381` | Non-input requests (drop, transfer). |
| Kyusu: schema sheet for any data subtype, `IDataSubtypeAuthoring`, field-path problems, `FixedTickPreview`, prefab "Reset to Source", movement resolve view with "set by …" | `DataProperties.h`; `DataSubtypeAuthoring.h:43-97`; `ProblemList.h:25-33`; `ComponentPropertySource.cpp:315-415`; `MovementPreviewProperties.cpp:34-132` | Effective-definition views with provenance have a precedent. |

### A.2 Needs a narrow extension

1. **One composition operator with keyed collections.**
   - `SceneComposition.cpp:52-68` replaces arrays whole, so a fragment can't
     add one grant.
   - The only `extends` is fact-schema-specific (`AnimFactSchema.cpp:115-127`).
   - The operator moves to `core`, because data assets compile in the runtime.
2. **Prefab `extends` without a wrapper.**
   - Today a variant places the base as an instance, which adds a synthetic
     root (`SceneComposition.h:17-21`).
3. **AbilityKit grant source, entry identity, parameters.**
   - **No source port:** no port reaches the grant's source
     (`AbilityDeclaration.h:31-43`).
   - **No parameters:** abilities take none (`:338-355`), and an entry
     overrides only bindings (`AbilityGrantEntries.h:16-24`).
   - **Wire entry identity is not unique** (see A.4).
4. **Costs beyond the actor's attributes.**
   - Only Instant Add effects on the actor are accepted
     (`EffectCost.cpp:150-202`).
5. **Grant capacity and ordering.** See A.4.
6. **Sustained presentation outside abilities.**
   - Extract the held/desired diff from `AbilityPresentationSystem`.
7. **Cosmetic body leases** for section hide, material slot overlay, and
   attach.
   - Only `audio.play`, `audio.loop` and `anim.present` exist
     (`AudioVerbs.cpp:42-45`).
8. **Safe mesh and material swap.**
   - Handles are owning (`ComponentAssetOwnership.h:60-72`). An in-place
     write desynchronizes the lease, and remove + add means two migrations.
9. **Replication relevance per entity.**
   - Interest is zone-granular only (`ReplicationChangeStore.h:76-83`).
10. **Entity references across snapshots.**
    - References resolve only within one message. A target sent later arrives
      as no entity, and nothing retries (`networking.md:696-707`).
11. **A persistable entity reference field kind.**
    - Planned as `StableEntityRef` (`unified-runtime-world.md:1054-1057`), but
      absent: `EntityId` fields have no scene codec, and only `Parent` is
    special-cased (`EntityBuildPackage.h:59-66,149-160`).
12. **Runtime `PersistentEntityId` mint.**
    - Reserved (bit 63) and deferred until "a dropped item [needs] to persist"
      (`runtime-stable-identity.md:145-149`). This design is that trigger.

### A.3 Gaps

- **No inventory, item, container or equipment code.**
- **Presentation gaps:**
  - no bone sockets or joint world-transform query;
  - no leader pose (`AnimPosePool.h:34-36`);
  - no viewmodel pass (Track B);
  - `CameraExclusion` covers one entity only.
- **Infrastructure gaps:**
  - no save system (Track A item 8);
  - no generic rollback (`PawnStateReplay.h:53-58`);
  - no AI behaviour selection (Track A item 7). The guard's *definition* is in
    scope, its *behaviour* is not.

### A.4 Defects and hazards found (all read, none reproduced yet)

1. **Nested prefab instances spawn outside their group.**
   - The cook stamps `scene_instance` on expanded members
     (`DocumentCookInput.cpp:354-362`).
   - The spawn's `SceneInstance` add is refused as a duplicate
     (`SceneSpawnService.cpp:158-163`; `EntityBuildPackage.h:182-197`).
   - Those members miss `Entities`, `Root`, `OwnedByRoot` and net despawn.
2. **Grant entry wire identity collides.**
   - `Intern` dedupes on (key, ability, override) (`AbilityGrantEntries.cpp:63-73`).
   - `FindByWireKey` returns the *first* entry whose key hash and ability match
     (`:80-86`).
   - Failure case: two assets declaring a grant with the same key for the same
     ability but different overrides. The client binds whichever it interned
     first. Interning order follows load order, so the client's predicted input
     resolution can differ from the authority's.
   - Parameters would make this worse.
3. **The pass grants before it revokes.**
   - `SyncEffectGrants` runs before the sweep loop's
     `RevokeEndedEffectGrants` (`AbilitySystem.cpp:308-309` vs `:345-347`).
   - With a full grant table, swapping a weapon fails to grant the new
     weapon's abilities for one pass, because the old grants still hold the
     room.
4. **Effect grants fail silently.**
   - `SyncEffectGrants` ignores `GrantAbility`'s refusal, retries every tick,
     and traces nothing (`AbilitySystem.cpp:254-260`).
5. **The tag serializer persists contributed stacks.**
   - `WriteGameplayTags` writes every tag with its full count
     (`GameplayTagSerialization.cpp:19-35`).
   - By contrast, `AbilityGrantsSerializer` writes Authored grants only
     (`AbilityGrantsSerializer.cpp:167-185`), and `WriteAttributes` writes
     `Base` only (`AttributeSerialization.cpp:13-29`).
   - Failure case: any restore followed by reconciling effects doubles every
     effect-granted stack. One stack then leaks forever on unequip.
6. **Clients instantiate every prefab member.**
   - `NetPrefabSpawner::Instantiate` imports the whole package
     (`NetPrefabSpawner.cpp:104-150`).
   - A member the authority also replicates as its own entity would exist
     twice on the client. This is inferred, not reproduced; Stage 4 reproduces
     it. The resolution is the authority-materialized declaration (§B.7.6).
7. **Effect-scaling costs at equipment scale.**
   - `SyncEffectGrants` scans every effect every authority tick
     (`AbilitySystem.cpp:230-262`).
   - The fold collects and sorts every non-periodic effect every tick
     (`EffectSystem.cpp:176-200`).
8. **`Override` resolves by registration order.**
   - `EffectSystem.cpp:191-212`.
9. **Stale doc.**
   - `docs/ecs/command-buffers.md:69-80` (`CreateEntity` now returns
     `PendingEntity`).
10. **A hot-reloaded effect's grants are duplicated.** Found during Stage 0
    planning; read, not reproduced.
    - A reload re-runs `DeclareEffectGrants`, which interns the edited entry
      and replaces the effect's entry list (`AbilityEffectGrants.cpp:25-41`;
      `ContentVocabulary.cpp:227-240`).
    - `RevokeEndedEffectGrants` revokes only grants whose effect has ended
      (`AbilitySystem.cpp:264-274`), so the old entry's grant stays held.
    - `SyncEffectGrants` then adds the new one beside it.
    - The same reload also leaves the stale entry first in the table, which is
      a second route to A.4.2.

---

## B. Recommended conceptual model

| Concept | Kind | Lifetime | Owner | Replicated? | Persisted? |
|---|---|---|---|---|---|
| **Composition** | Authoring/cook operation | Cook (prefabs); data-asset compile | One core operator | Output is ordinary content | Sources only |
| **Item definition** | `item.definition` data asset | Content lifetime, reloadable | Item registry (`ItemId`, wire key from path) | By wire key | By asset path |
| **Item entry** | Entity: `ItemEntry` + instance components + `PersistentIdComponent` | Creation until reaped | Inventory | To peers relevant to its container | Yes |
| **Container** | `ItemContainer` on any entity | The entity's | Inventory | Rules are authored; entries as above | The entity's |
| **Pickup** | World entity; `ItemPickup` names an entry or a (definition, count) | Until taken | Inventory | Like any world entity | Zone state |
| **Equipment record** | Entry in `EquipmentState` on the receiver; slots from `equipment.layout` | Equip to unequip | Equipment | Definition and slots to all; source owner-only | Yes: the record, never its effects |
| **Contribution** | Effects, leases, presentation holds, owned by `LeaseOwner{source, record key}` | While the record or cause lasts | AbilityKit, lease providers, presentation | Grants, attributes and tags as today; effects never | Never |

```text
authored                    runtime (authority)                          derived
─────────                   ───────────────────                          ───────
prefab ─extends/includes─▶  entity (ordinary components)
item.definition ─extends─▶  ItemEntry{Container, ItemId, Count} ──equip──▶ EquipmentState record
                            (+ instance components)                       │ (slots mask)
equipment.layout ─────────▶ slot rules, binding overlay, order, socket    ▼
                                         authority: ActiveEffect × N owned by {source, record}
                                           ├─ modifiers → AttributeSet.Current
                                           ├─ tags → contributed stacks
                                           └─ grants {entry identity, Source, params} → abilities
                                         every machine: presentation holds keyed by record
```

### B.1 Composition rules

- **R1. Ambiguous static conflicts are errors.**
  - Two fragments setting one field differently fail at that field.
  - The including definition resolves it by stating the value. That local
    value *is* the explicit precedence (§B.6).
  - Precedence: local beats fragments; fragments beat the base.
- **R2. The schema declares merge policy.**
  - Scalars and records override.
  - Arrays keyed by `StableKey` or a declared key field (a tag's or attribute's
    `name`, a grant's `key`) merge member-wise, with removal.
  - Unkeyed arrays replace whole.
  - Components with custom scene forms (`AbilityGrants`, `Attributes`,
    `GameplayTags`) declare their array key beside their serializer.
- **R3. Absent, cleared and set are distinct.**
  - Absent means inherited.
  - `null` clears an optional, or removes a keyed member.
  - A present value is an override even when it equals the base.
  - Definition documents write records; placed instances keep today's harvest
    diff.
- **R4. Removing an owed component fails at compose time** (`removes_owed`).
  - Runtime removal is unchanged.
- **R5. `abstract` definitions** skip required-field validation and are refused
  as grant or spawn targets. This keeps Dark's archetype/concrete split without
  negative ids.
- **R6. Composition knows nothing about items.**
  - It lives in `core`, operates on schema-described values, and is
    consumed by scenes and data assets.

### B.2 Grant identity, source and selection (review point 1)

**Entry identity is content-qualified.**
- An `AbilityGrantEntry` gains an **origin**: the asset that declared it, which
  is a prefab, an effect definition, or an item definition.
- It also gains an optional **overlay**: an `equipment.layout` asset plus a
  slot key, when a slot refines the binding.
- Its **parameters** are part of its content.
- Identity is the triple `{origin, key, overlay}`, and the wire carries those
  three as name wire keys, beside the ability's.
- A client resolves the triple against its own content:
  - the origin's declared entries;
  - the layout's slot;
  - then it interns the combination.
- So distinct parameters or overrides can never resolve to the wrong entry: a
  difference in content means a difference in origin or key.
- If the client lacks the origin asset, it falls back to today's behaviour:
  "held but bound to no input".
- `FindByWireKey(key, ability)` is deleted.
- **Wire cost:** about 8 B more per grant on the owner-only grants run.
  - Stage 3 adds a test that a fully loaded pawn's first send fits a datagram.
    None exists today.
  - If it does not fit, the triple is hashed into one word, and a collision
    among interned entries is refused at intern time on the authority.

**Instance-varying values never go in parameters.**
- Rolled damage, durability and enchantments are components on the source.
- Abilities read them through the `source` port.
- Parameters are the per-definition numbers, the same for every instance of a
  definition.

**Selection uses AbilityKit's existing priority, supplied per slot.**
- An `equipment.layout` slot's binding overlay can set `priority`.
- Example: two rings, each granting Fireball on `Input.Cast`, in
  `ring.left` (priority 20) and `ring.right` (priority 21).
  - A press starts the right ring's activation, with source = right ring.
  - With the right ring removed, the next press starts the left ring's.
- An equal top priority stays `BindingAmbiguous`, exactly as today. It becomes
  a layout check in Kyusu ("ring.left and ring.right tie on Input.Cast"), and an
  equip-time warning in dev.
- **Nothing collapses silently.**

**Requests choose the same way.**
- Today `ability.activate` takes the first grant whose ability matches
  (`AbilitySystem.cpp:211-223`).
- It becomes: the highest effective priority, then the lowest grant id. An
  optional `source` argument names the grant exactly.

**Aggregation belongs to effects, not grants.**
- "Two rings make Fireball stronger" means each ring's maintained effect adds
  to `SpellPower`, and the ability reads the attribute.
- An ability that wants every source evaluates `equipment.sources {receiver,
  tag}` into an `entities` local.
- An activation never has multiple sources.

**A source that has not arrived, or is not relevant.**
- On the authority, `source` is the live entity, or dead and closed.
- On the owner's client, grants (owner-only) and entries (owner-relevant)
  stream in separate snapshots. Today a reference to a not-yet-received entity
  decodes to no entity, and is never re-decoded.
- **Generic fix (A.2.10):**
  - the applier remembers which components decoded an unresolved
    `NetEntityId`;
  - it re-applies their last image when that id binds.
  - The list is bounded and per peer.
  - Consumers: grant `Source`, `ItemEntry.Container`, record `Source`,
    `ItemPickup.Entry`, and today's `AnimRequestSet` entity fields.
- A source never relevant to this peer (a shrine's aura from an uninterested
  zone) reads as none.
  - The compile rule already requires an `entity` guard on entity arguments
    from locals. It extends to `source`.
  - A predicted op skips closed, and the authority's result stands.
  - Abilities whose prediction depends on source state should predict at
    `presentation`.
- Input binding never depends on the source's arrival, because entry identity
  comes from content.

### B.3 Atomic costs: cost providers, not transactional verbs (review point 2)

The commit is already one transaction (`AbilityCommit.cpp`):
- every cost check, then the cooldown room check;
- then every apply;
- nothing in between.

**Extend the cost list, not the verb contract.**

```text
cost group: { key, timing, repeat,
              effects:   [effect...]                         // existing provider
              resources: [{ provider, arguments }] }          // registered providers
provider:   { name, argument schema,
              Check(const World&, actor, batch) -> CostCheck  // whole batch, aggregates itself
              Apply(World&, actor, batch) }                   // infallible after a passing Check
```

**Guarantees:**

1. **Preflight is complete.** The commit gathers every payable group's costs,
   batches them per provider, and calls each `Check` once with its whole batch.
   - Effect costs are the built-in provider. Their per-attribute summing
     generalizes to per `(target, attribute)`.
   - Effect costs gain `target: actor | source`, so HL2's clip lives on the
     weapon entry.
   - The item provider sums per `(entry or container, definition)`, so two
     groups each taking one arrow check for two.
   - The cooldown's room check stays as today.
2. **Apply cannot fail.**
   - Check and Apply run in one call on the owner thread, with nothing in
     between.
   - A provider whose apply could refuse must detect that in `Check`.
   - `Apply` asserts its batch is still valid in debug builds.
   - Apply order is fixed: effects first, then providers in the definition's
     order.
3. **Apply makes no structural change beyond today's.**
   - The item provider sets `Count`, possibly to 0, which is *depleted* (§B.8).
   - It never destroys an entry.
   - Effect costs are Instant and create no entity.
4. **Item and attribute costs never partially commit.** Both are checked
   before either applies.
   - A provider touching two resources checks both itself.
   - Two providers never share a resource: the item provider owns entry
     counts, the effect provider owns attributes.
5. **Prediction.**
   - A guess calls `Check` against replicated state (owner-relevant entries,
     owner-only attributes) and never `Apply`. This is today's "validates but
     pays nothing".
   - A stale client count makes the guess differ. The authority decides, and
     the guess ends `AuthorityDenied` as today.

**Why not commit-checkable verbs.**
- Verbs are dispatched through binding tokens to arbitrary providers and have
  no check half.
- Giving every verb transaction semantics would make every provider answer
  "could you?" for one consumer.
- Providers are a registered operation table scoped to costs. They are
  declared by subsystems (inventory) or games (a durability provider), and
  validated at ability declaration like verb bindings.
- **Known consumers:** item counts (Skyrim arrows, Zelda bottles), and effect
  costs on the source (HL2 clip).

### B.4 Reconstruction: authority, client, persistence (review point 3)

**Authority, gameplay.**
- One idempotent **`ReconcileEquipment(receiver)`**:
  - desired = for each record, its definition's maintained effects, with
    scale and overlay, owned by `{source, record key}`;
  - present = effects whose owner matches;
  - it removes the extras and applies the missing ones.
- It runs on:
  - equip and unequip (which *is* their apply step);
  - spawn or zone import of an entity carrying authored `EquipmentState`;
  - a future save load;
  - a dev definition reload;
  - the dead-source sweep.
- It is never "apply again", so a partial restore cannot double anything.
- **Cost:** a scan of all effects for the owner match, at event time only.
  Measured in Stage 0.

**Clients, presentation and prediction only.**
- Clients never reconcile gameplay; effects do not exist there.
- They receive `EquipmentState` (to all), and grants, attributes and tags
  (to the owner).
- From `EquipmentState` and shared tags they derive presentation, with
  `AbilityPresentationSystem`'s first-sight rule: a late joiner presents
  current state, not equip transitions.
- The owner predicts abilities from replicated grants (entry identity,
  `Source`) and costs by `Check` only.
- A listen host is the authority.

**Persistence stores baseline, never contributed state.**

| State | Today | Required |
|---|---|---|
| Attributes | `Base` only; `Current` derived (`AttributeSerialization.cpp:13-29`) | ✓. Instant and periodic Base changes are baseline, correctly. |
| Grants | Authored only (`AbilityGrantsSerializer.cpp:167-185`) | ✓ |
| Effects | Runtime only (D-H) | ✓. Reconciled from records. |
| Tags | **All stacks** (`GameplayTagSerialization.cpp:19-35`) | ✗. Separate baseline stacks from contributed stacks; write baseline only (D9). |
| `EquipmentState`, entries | — | Records and entries persist through schema plus persistable references |

**D9 (approved): the container keeps a baseline count per tag beside the
total.**
- Effects, ability owned-tags and systems change only the contributed part.
- The serializer writes baseline only.
- Replication keeps sending totals.
- Cost: +64 B per tag container, and an ABI bump.
- The API splits in two:
  - `GrantBaseline`/`RevokeBaseline`: persistent facts;
  - `Grant`/`Revoke`: contributions.
- No caller picks a side implicitly: the existing `Grant`/`Revoke` keep their
  names and become contribution-only.

**D9 audit: every writer.** Found by grep for `GameplayTagContainer` and its
`Grant`/`Revoke`/`Clear` callers on main.

| Writer | Where | Today | Becomes |
|---|---|---|---|
| Scene load | `GameplayTagSerialization.cpp:41,61` (`ReadGameplayTags`) | `Clear` + `Grant(stacks)` | `GrantBaseline` |
| Scene save | `GameplayTagSerialization.cpp:19-35` | writes total counts | writes baseline counts |
| Level-editor tag edits | `GameplayVocabularyEdits.cpp:51-79` | `Grant`/`Revoke` on document entities | `GrantBaseline`/`RevokeBaseline`. Document entities are pure baseline. |
| Effect granted tags | `EffectSystem.cpp:95,260` | `Grant`/`Revoke` | contribution (unchanged call) |
| Ability owned tags | `AbilityPass.cpp:221,366` | `Grant`/`Revoke` | contribution |
| Movement `Grounded`, derived each tick from `SupportState` | `MovementModeSystems.cpp:224-226` | `Grant`/`Revoke` | contribution; recomputed after restore |
| Movement active-mode tags (`LocomotionModeTransitionSystem::StepImpl`) | `MovementModeSystems.cpp:176-186` | the target mode's `ActiveTag` granted and the others revoked, **only on a transition** | contribution. `CharacterMovement.Mode` is persisted baseline, but the tag is written only when a transition fires, so a restored character in a non-default mode would lack its tag until the next transition. Stage 5 makes the tag a pure function of the persisted `Mode`, the way `Grounded` is projected from `SupportState`, and adds the regression test: save and restore a character in an active mode, and its tag is present with no transition. |
| Movement mode request tags (a one-tick mailbox) | `MovementModeSystems.cpp:49` | `Revoke` (consume) | contribution. A request pending at save is not persisted: one tick is lost, which is stated and accepted. The consumer revokes a contribution, so a baseline request tag would never be consumed. Requests are therefore contribution-only by rule. |
| **Replication decode** | `GameplayTagContainerWire.cpp:41-47` | `Clear` + `Grant(total)` | **contributions only, baseline left at zero**. A replica has no baseline, so saving a received container writes nothing. A received effective count can never become saved baseline state. |
| Ability Workbench preview fixture | `AbilityPreviewSession.cpp:243-245` | scripted `Grant`/`Revoke` | contribution; an editor-only World that is never saved |
| Movement preview sims | `MovementResolvePreview.cpp:157` | own storage | unaffected |

**Readers** (conditions, queries, animation predicates, perception, candidate
measures, movement profile layers, the debug panel, the cooldown room check)
all read **totals**. None changes, and the 32-tag capacity applies to totals.

**Same audit for the other AbilityKit state:**
- `AbilityGrants` decode on a client carries `Kind`, so a replica's Authored
  grants are the authority's baseline. Clients never save.
- `AttributeSet` decode carries the authority's `Base`.
- On a listen host the authority is the saver, and it decodes nothing.

**Test (Stage 5):** a live equipped entity serialized and restored through the
scene serializer, then reconciled, has identical tag counts, grants and
attributes.

### B.5 Equip effects are maintained; equip is atomic (review point 4)

**Only maintained contributions are reversible.**
- An Instant effect writes `Base` once. A periodic effect writes `Base` each
  period (`EffectSystem.cpp:148-153`).
- Neither can be withdrawn. Therefore:
  - `equip.effects` must name **Infinite** effects. Anything else fails
    compile (`equip_effect_not_maintained`).
  - A periodic Infinite effect is allowed, but it is a *process*: the sheet
    labels it "changes Base every N s; past changes stay after unequip". The
    regeneration ring is the case.
  - One-shot actions are a separate, explicitly non-reversed list, added only
    for a known consumer. The one consumer in the exercises is HL2's ammo on
    pickup: `take.effects` (Instant only), with `keep: false` for an item that
    never becomes an entry.
  - There are no `on_equip` one-shots until a consumer appears; a deferred
    entry records the trigger.
- A reload that turns an equipped effect non-maintained makes reconcile unequip
  the record, with a diagnostic.

**The equip check covers every refusal before anything changes.** It computes
the state after the replacement:

| Resource | Check |
|---|---|
| Layout | slot exists; every slot in `occupies` accepts the definition; occupants are whole records |
| Receiver tags | `lock_tags` absent; room in the 32-tag container for the new effects' fresh tags, with occupants' stacks removed |
| Grants | room in the 16-grant table for the new grants, counting occupants' grants freed and outstanding reservations |
| Grant entries | room in the World's entry table to intern overlay entries |
| Attributes | every attribute the new effects modify exists on the receiver (`MissingAttribute`: fail closed, like costs) |
| Records | room among 16 records |
| Definitions | every effect resolves and is maintained |

**Apply, in one call with no refusal path:**
1. Remove each occupant's effects. Tags are revoked now.
2. Clear the occupant records.
3. Write the new record.
4. Apply the new effects. Tags are granted now; grants are reserved.

**Two AbilityKit corrections make grants atomic across the tick boundary:**
1. **Revoke before grant in the pass:** `RevokeEndedEffectGrants` for all
   actors runs before `SyncEffectGrants`. Freed room is then usable the same
   pass, and an old weapon's activations and the new weapon's grants change on
   the same tick.
2. **Grant reservation:**
   - applying a grant-bearing effect records its grant count against the target
     in a small World-owned reservation list;
   - `ApplyEffect` refuses a grant-bearing effect whose reservations would
     exceed the room;
   - `SyncEffectGrants` consumes reservations;
   - a sync failure becomes impossible for reserved grants, and is traced
     otherwise (A.4.4).
   - Grants stay deferred to the pass. Granting mid-pass is the class of bug
     the third AbilityKit review fixed (`6ff56c4d`).

### B.6 Conflicts and precedence (review point 5)

Explicit, deterministic, inspectable precedence is legitimate. AbilityKit's
binding priority is the existing example. Three categories:

| Category | When | Rule |
|---|---|---|
| **Static composition** | Authoring/cook | Ambiguity (two fragments disagree) is an error at the field. The explicit precedence is a local value. Kyusu offers "take X's value", which writes one. **No include-order precedence:** reordering a list would silently change results, and at authoring time the author is present to decide. |
| **Runtime modifiers** | Every fold | The phase order Add → Multiply → Override stays. `Override` modifiers gain an authored **`precedence`** (default 0): highest wins. At equal top precedence the fold stays deterministic (definition path order, then application order, as today) and is **diagnosed**: a dev trace once per attribute and pair, and the Kyusu equip preview flags it. Example: `Frozen` (MoveSpeed → 0, precedence 100) beats `Sprint Boots` (MoveSpeed → 8, precedence 10). Default 0 keeps today's behaviour. |
| **Presentation layering** | Presentation diff | Coverage hides are unions, so order is irrelevant. Material overlays and attachments to a shared socket follow the receiver layout's slot **`order`**. Ties on overlapping coverage are a layout-authoring diagnostic. |
| **Input** | Press | Existing binding `priority`, now also supplied by slot overlays. Ties remain `BindingAmbiguous` and are diagnosed. |

All four have the same shape: an explicit number, a deterministic fallback, and
a diagnosed tie.

### B.7 Identity and ownership transitions (review point 6)

1. **Every entry has persistent identity from creation.**
   - Authored entries use the editor mint.
   - Runtime entries use the runtime `PersistentEntityId` mint (bit 63), built
     in Stage 4, with its counter as world state the save system will persist.
   - Containers holding persistent entries have persistent ids too. A
     runtime-spawned pawn takes a runtime id.
2. **References are persistable.**
   - `ItemEntry.Container`, record `Source` and `ItemPickup.Entry` use the new
     persistable reference kind (A.2.11).
   - Each is an `EntityId` at runtime and a `PersistentEntityId` in scenes and
     saves, resolved through `PersistentEntityIndex` at import.
   - This is generic, with other known consumers such as switch → door links.
3. **One function moves an entry: `MoveEntry(entry, destination)`.** It:
   - writes `Container`, or none when dropped;
   - moves the entry to the destination's partition: the container's
     partition, or the drop point's zone;
   - updates net ownership and relevance;
   - may split a stack, in which case the *moved* part is a new entry and the
     remaining part keeps its identity;
   - removes the entry from its spawn group (`SceneInstance`) the first time it
     leaves the container it was authored in. Otherwise a purse pickpocketed
     from a spawned guard would be destroyed with the guard by `OwnedByRoot`
     despawn (`SceneSpawnService.cpp:391-418`).
   - A future zone-state overlay observes one transition.
4. **Authored contents are authored entry entities, not a spawn-time list.**
   - Revision 1's `InitialItems` is withdrawn. A list on a level-placed chest
     would be re-processed on every zone import, while the entries taken from
     it live on in the player's persistent partition. That duplicates items.
   - Authored entries taken out of a zone are already recorded Destroyed at
     detach and suppressed on re-import (`RuntimeWorld.cpp:340-371`;
     `ZonePackageImporter.cpp:301`). Stage 4 adds the test.
5. **Dropped entries in a zone are a temporary limitation, not the
   semantics.**
   - Today a detach destroys dynamic entities.
   - The zone-state overlay (Track C item 5) needs to capture dynamic entities
     with persistent ids and their components.
   - Entries need nothing more: same storage, same identity, same references.
6. **Entries are authority-materialized (D12, revision 3).**

   **The problem.** Today every machine imports a level's authored entities.
   Replication then binds the authority's copy to the client's own by
   persistent id, and the applier refuses to create an authored entity the
   client lacks, because "creating one would be the duplicate"
   (`ReplicationSnapshot.cpp:975-995`). That is right for doors. It is wrong for
   entries, which are private to whoever is relevant to their container:
   - every client would hold copies of every chest's and NPC's contents, and
     those copies go stale as soon as someone loots them;
   - the same holds for net prefab spawns, which instantiate every member
     (A.4.6).

   **The rule.** A component type may declare at registration that entities
   carrying it are **authority-materialized**. `ItemEntry` declares it. The
   declaration has exactly three consumers:

   | Consumer | Authoritative machine | Non-authoritative machine |
   |---|---|---|
   | Content import (`ImportPackageIntoPartition`, used by level and zone import and by `SceneSpawnService`) | imports everything, as today | skips entities carrying an authority-materialized component |
   | Net prefab instantiation (`NetPrefabSpawner::Instantiate`) | — | skips the same members, which fixes A.4.6 for any such member |
   | Snapshot writer | does **not** send the authored persistent id for such entities (`sendAuthored`, `ReplicationSnapshot.cpp:493`) | — |

   **Lifecycle on a client:**
   - **Materialized.** A relevant entry arrives as an ordinary bare spawn,
     through the applier's existing `CreateEntity` path
     (`ReplicationSnapshot.cpp:1349-1360`). Its `Container` resolves through
     deferred entity-reference resolution (§B.2).
   - **Updated.** Ordinary deltas.
   - **Removed.** When relevance ends, the authority records an owed destroy for
     that peer: the existing per-peer `OwedDestroys` channel, "cleared only by
     proof" (`ReplicationSnapshot.h:134-188`). It is the same thing a destroy on
     the authority produces. There is no client self-destruction and no special
     case.
   - **Re-relevant.** The peer no longer knows the id, so the entry arrives as a
     new bare spawn.

   **What is preserved:**
   - The applier's refusal stands unchanged. An authored id the applier sees
     always belongs to a content-materialized entity the client imported
     itself, so duplicate protection is intact and is now guaranteed for the
     other kind as well: an authority-materialized entity has exactly one
     materialization path on each machine.
   - Persistent identity lives where it is needed: on the authority, for saves
     and zone state. A client replica carries none, and clients never save.
   - Standalone play and listen hosts import everything. The editor imports
     everything.

   **Why a declared policy rather than an inventory special case.**
   - The three consumers are generic engine paths that must not name inventory.
   - Making it per-type rather than per-entity means an authored entry can't be
     mismarked.
   - The cost is one flag on component registration and one predicate in each
     consumer.
   - Any future component whose entities are private to some peers (a hidden
     objective, a secret container) declares the same flag.

   **New relevance mechanism.** Per-entity relevance itself (owner, later
   observer) is a new mechanism. It is a filter in the change store's per-peer
   send, beside zone interest, and its loss produces owed destroys.

### B.8 Records, depletion and reaping (review point 7)

**One record per logical equip.**

```cpp
struct EquipmentRecord { std::uint16_t Key; ItemId Definition; EntityId Source;
                         std::uint32_t Slots; std::uint8_t Primary; };
```

- `Slots` is a mask over at most 32 layout slots. `Key` is per receiver and
  never reused.
- A two-handed sword is one record with two slot bits, and one set of
  maintained effects owned by `{source, key}`.
- Unequipping either slot removes the record.
- The binding overlay comes from the `Primary` slot, which is the first entry
  in `occupies`.
- `Source` may be the receiver itself or any entity, for a curse or a vehicle
  module with no entry.

**Quantity, charges and contents are three different things (D11,
revision 3).** A definition states which it uses. Revision 2 conflated them by
treating an empty bottle as a depleted entry.

| Kind | Represents | Lives in | Spent by | At zero |
|---|---|---|---|---|
| **Quantity** | How many interchangeable units: arrows, bombs, coins-as-items, potions in a stack | `ItemEntry.Count` | The `item` cost provider (`{ entry \| slot \| container, count }`) | **Depleted**: transient, always reaped (below) |
| **Charges** | One instance's expendable resource: lantern oil, wand charges, a medkit's units, sips | An attribute on the entry's `AttributeSet`, seeded from the definition's `attributes`; clamped and persisted as `Base` like any attribute | Effect costs with `target: source` (D10), refilled by effects on the entry | The item persists: "an empty wand". Disposing of it is authored in the ability: a commit guarded `when Charges ≤ cost` also pays `{ item: source, count: 1 }`, in one transaction. There is no implicit empty rule. |
| **Contents** | Items held *by* an item: a bottle's fairy, potion or milk | Entries in the item's own `ItemContainer` (capacity 1, accepted tag query); nesting is already in the model | The `item` provider with `{ container: source, count: 1 }` | The container item is unchanged. An empty bottle is one bottle with no contents, still assigned. |

A Zelda bottle is therefore one bottle entry (quantity 1, never depleted)
holding a contents entry. Drinking consumes the contents. Catching a fairy puts
a fairy entry into the bottle. Milk with two sips is a contents entry with a
`Sips` charge. An arrow stack is quantity.

**Depleted quantity.**
- The commit never destroys a source (§B.3.3): it leaves `Count == 0`.
- Depleted entries are hidden from `inventory.count`, container views, UI
  models and saves.
- An **inventory reaper** runs on the authority each fixed tick, after
  `AbilitySystem`, over the bounded list of depleted entries. For each:
  1. **Wait** while a live activation runs from a grant whose `Source` is the
     entry. The test is AbilityKit's `HasLiveActivationFrom(actor, source)`, a
     read over records and grants. This is the case of an arrow stack whose
     last arrow was just fired.
  2. **Then**, if a record holds it, apply the slot's `on_depleted`:
     - **`release`** (default): the record is removed through
       `ReconcileEquipment`.
     - **`hold`**: the record keeps its definition and slot, and its `Source`
       becomes none. Its maintained effects are withdrawn, so it grants nothing.
       The HUD still shows the assignment. When an entry of that definition next
       enters one of the receiver's containers, inventory rebinds the record to
       the first such entry in container order and reconciles. That covers
       Zelda's bombs on a button at zero, and HL2's grenade slot uses
       `release`.
  3. **Destroy** the entry.
- **Unassigned depleted entries** follow steps 1 and 3 only. They are reaped
  as soon as nothing runs from them. Disposal never depends on a timer, a UI
  or the definition.
- **Refill.** Stacking never revives a depleted entry: a pickup of the same
  definition merges into a non-depleted entry or creates a new one. A `hold`
  record then rebinds to the new entry.

**Other source endings:**
- **Transferring or dropping an equipped entry** unequips it in the same
  transaction. `lock_tags` can refuse the whole operation.
- **Destroyed by other code:** the equipment sweep clears records whose
  `Source` is dead. This is O(records) per equipped receiver per tick, bounded
  by 16. The effects end by owner liveness, the grants are revoked, and the
  activation ends `GrantRevoked`, two ticks later.

**Use from a menu** (Skyrim potions) goes through a **transient request
grant**:
- `inventory.use {entry}` asks AbilityKit to activate the definition's `use`
  entry with `Source = entry`;
- the grant has Kind `Request` and ends with its activation;
- the same identity, source and parameter rules apply, and the same depletion
  path follows.

---

## C. Alternatives and tradeoffs

### C.1 The three approaches

| | 1. Item-definition inheritance only | 2. General prototype facility | 3. Extend prefabs and data assets (**recommended**) |
|---|---|---|---|
| Adds | `extends` on `item.definition` | New asset kind with component sets, features, merge rules; instantiates entities | One core operator; `extends`/`includes` on both formats |
| Guard variants | Not covered | Covered | Covered |
| Duplicates | A third `extends` | A second entity format, plus spawn, cook, inspector and net-spawn identity (`NetSpawnPrefab` names scenes) | None; retires the fact-schema special case |
| Useful without inventory | No | Yes | **Yes**: prefab, ability, movement and cue variants |
| Risk | Under-delivers | A competing object model | A format change (D2, approved) |

### C.2 Compile-time versus runtime inheritance

| | Compile-time (recommended) | Runtime (Dark / opde) |
|---|---|---|
| Per-frame cost | Zero | A donor lookup, or a cache sized ids × properties |
| Inspectability | Effective view with provenance in Kyusu; plain runtime entity | Needs donor tooling |
| Parent changed in play | Data assets re-compose the dependent closure on reload. Live prefab spawns don't update (true today). | Reads see it; opde still misses field edits |
| Runtime "is-a" | A contributed tag | Native |
| Runtime attach and detach | Effects and leases | Native, priority-ordered |

### C.3 Inventory storage: entries as entities (provisionally approved)

| Constraint on main | Component on owner | Out-of-line pool | **Entity per entry** |
|---|---|---|---|
| First send fits one datagram; an entity is written whole (`ReplicationSnapshot.cpp:747-760`) | Caps at roughly 60 entries, sharing with the pawn | New variable-length path | Each entry is small; any count streams |
| Typed per-instance state | Property bag or cap | Property bag or second schema system | Ordinary components |
| Persistence | New serializer | New serializer | Schema + persistent id |
| Effect ownership | Index key that shifts | Handle | `LeaseOwner{entry, record}` |

**Correction to revision 1.** Revision 1 said entries cost nothing per tick.
That is true of gameplay, but **false of replication**:
- `ReplicationChangeStore::Update` walks every `NetReplicated` entity on every
  publish.
- For each one it does a map lookup and re-encodes each replicated component
  to compare (`ReplicationChangeStore.cpp:87-120`).
- So a replicated entry costs authority CPU at the publish rate (30 Hz), plus
  change-store memory and a floor per peer, even when idle.

**Mitigation, by design:**
- an entry carries `NetReplicated` only while some peer is relevant to its
  container (the owner, and later an observer);
- an NPC's purse or an unopened chest's contents are not replicated at all;
- the replicated population is roughly the players' own inventories.

**Remaining risk.** Hundreds of entries per player across 16 players. Stage 0
measures the cost.

**Fallbacks, if the measurement fails the gate:**
- skip unchanged chunks in the change store by chunk change version. This is
  a generic replication improvement and is not done before measurement;
- page entries into fixed-size page entities;
- reconsider the pool, with its own wire path.

---

## D. Lifecycle walkthroughs

Unless a step says otherwise, it runs on the authority.

### D.1 Item definition through inheritance

1. **Author three definitions.**
   - `Clothing`, abstract: occupies `body.torso`, presents a torso hide, tag
     `Item.Wearable`.
   - `Armor`, extends `Clothing`: adds maintained equip effect `e_armor`
     (`armor_rating.sdata`, Infinite, scale 10).
   - `LeatherTunic`, extends `Armor`: sets name, world prefab, material
     overlay and icon; patches `e_armor.scale` to 14.
2. **Kyusu shows the effective sheet.** Each row reads *inherited (Armor)*,
   *inherited (Clothing)* or *set here*, with revert. Removed keyed members
   are struck through.
3. **Load.**
   - lease the base as a `Dependency` (cycle detection exists,
     `DataAssetLoader.cpp:133-170`);
   - compose;
   - validate the composed value;
   - compile.
   - The `equip_effect_not_maintained` check runs here.
4. **Errors** land at the field path (`$.data.equip.effects.e_armor.scale`).
   - Errors include: missing base, cycle, `include_conflict` (naming both
     fragments), and `abstract_definition` used as a target.

### D.2 A guard from a specialized prefab

1. **Author the chain.**
   - `humanoid.sscene`: body, rig, controller, `Attributes{Health 100}`, tag
     `Creature.Humanoid`.
   - `guard.sscene`:
     - `extends: humanoid`;
     - `includes: [alertable, combat_training]` (perception component, combat
       grants, tag `Creature.Humanoid.Guard`);
     - **two member entities: a sword entry and a purse entry**, each with
       `ItemEntry.Container` referencing the root;
     - an authored `EquipmentState` record (`hand.right`, Source → the sword
       member).
   - `elite_guard.sscene`: `extends: guard`; `Health` base 200 by a keyed
     patch; one extra keyed grant.
2. **Cook.** Composition with R1–R4, then flatten to `.smap`. Persistable
   references are written as scene-local ids, then remapped.
3. **Spawn or zone import.**
   - Ordinary entities.
   - The entries and records arrive as authored relations.
   - `ReconcileEquipment` applies the sword's maintained effects: the same path
     as load.
   - Level-placed guards carry persistent ids. Spawned ones get runtime ids.
4. **Clients.**
   - `NetSpawnPrefab{elite_guard}`. The sword and purse members are
     authority-materialized, so the client instantiates no copy of them
     (B.7.6).
   - `EquipmentState` arrives with the guard.
   - Presentation draws the sword in hand.
   - The guard's entries are relevant to no peer and are not replicated.
5. **Pickpocketing the purse** is `inventory.transfer`. Because it is
   authored, taking it out of the zone suppresses it on re-import (D.9).

### D.3 Picking up and stacking

1. **Request.** An interaction ability invokes `inventory.take {pickup:
   target, into: actor}`.
2. **Check:**
   - the pickup is alive;
   - the container accepts the item (its tag query);
   - there is room (capacity, or a game weight attribute);
   - the item stacks. Compatibility means: same `ItemId`, no instance
     components on either side, room under `stack.max`. A depleted entry is
     never a merge target (§B.8).
3. **Apply:**
   - add to an existing `Count`, or create entries through the inventory's one
     creation function (runtime persistent id, `MoveEntry`);
   - destroy the pickup, or `MoveEntry` the entry it named;
   - apply `take.effects` one-shots;
   - an item with `keep: false` creates no entry.
4. **Refusals:** `ContainerFull` or `NotAccepted`. Nothing changes.
5. **Client.** The owner-relevant entry replicates. The container index
   updates from `Changed<ItemEntry>`.

### D.4 Equipping a weapon (atomic replacement)

1. **Request.** `equipment.equip {receiver, entry, slot?}` arrives from an
   ability op, a UI action routed through a payload message, or authored data
   (reconcile).
2. **Check:** all of §B.5's table, computed after the replacement.
   - Example: a full 16-grant table swapping a two-grant weapon for a
     two-grant weapon passes, because the occupant's two are counted as freed.
   - Swapping for a three-grant weapon is refused (`GrantCapacity`), with
     nothing changed.
3. **Apply:** remove the occupants' effects (tags revoked now), clear their
   records, write the record, apply the new effects (tags now, grants
   reserved).
4. **Next pass:**
   - revoke the old grants first, then grant the new ones;
   - activations of the old weapon end `GrantRevoked`;
   - the new grants are live, on the same tick on every machine.
5. **Presentation.** Every machine diffs the receiver's record set and swaps
   the holds.
6. **Prediction (v1, D6).** Authority-only. A presentation-level ability can
   play the draw animation at once. Firing the new weapon waits for its grant,
   one round trip.

### D.5 Activating an ability supplied by equipment

1. **Press.** It resolves by priority as today. Slot overlays supply the
   priorities (§B.2).
2. **What the activation reads:**
   - `port: source`: instance components such as `game.durability.value`,
     guarded by `entity`;
   - `param: damage`: the definition-level number from the entry.
3. **Commit.** One batch:
   - stamina (effect provider);
   - one arrow (item provider, `{ slot: ammo, count: 1 }`, resolved through the
     actor's records);
   - any one failure means nothing applies (`CommitFailed`).
4. **Client guess.** `Check` only, against replicated entries.

### D.6 Unequipping while an ability runs

1. **`lock_tags`** can refuse ("finish the swing" as data).
2. **Otherwise:**
   - the record's effects are removed;
   - next pass, its grants are revoked;
   - the activation ends `GrantRevoked`: holds released newest first, no exit
     ops;
   - paid costs stay paid;
   - client guesses end with the authority's reason.
3. **The entry is untouched.**

### D.7 Applying and removing a temporary property

1. **An effect** gives modifiers, tags (`State.Burning`, `replicate_to_all`) and
   optional grants. It is applied with a duration, or held through
   `effect.hold` under its owner.
2. **A tag presentation** maps `State.Burning` to cosmetic leases. The shared
   sustained-presentation diff holds them on every machine.
3. **Removal** withdraws exactly this instance's tags and grants. Another
   source's stack keeps the tag.
4. **Thief's runtime metaproperty** maps onto an effect. Override interactions
   use explicit `precedence` (§B.6).

### D.8 Reloading a parent definition

**Data assets:**
1. Re-compose the dependent closure in dependency order, and bump each
   `ReloadVersion`.
2. Effect definitions redefine in place.
3. In dev builds, `ReconcileEquipment` re-derives records whose definition
   version moved: remove and re-apply that record's effects, since granted
   tags are recorded per instance.
4. A child broken by the edit keeps its last good value, with the error at its
   field.

**Prefabs:**
1. Kyusu re-projects open documents.
2. Live spawns don't change.
3. Stage 2 lists dependents for recook.

### D.9 Dropping, and zone round trips

1. **`inventory.drop {entry, at}`:**
   - unequip (D.6 rules);
   - `MoveEntry` to none at the drop point's zone partition;
   - spawn the world prefab with `ItemPickup{Entry}`.
   - The instance components stay, so an enchanted sword stays enchanted.
2. **Authored entry taken from a chest into the player's inventory:**
   - it moves to the persistent partition;
   - at chest-zone detach it is not live in the zone, so it is recorded
     Destroyed;
   - on re-import it is suppressed.
   - Result: no duplication, and it remains in the player's inventory. This
     works today and gets a Stage 4 test.
3. **Runtime entry dropped into a zone:**
   - it is destroyed at detach together with its pickup, which is the
     temporary limitation (D8);
   - the overlay will restore both with the same ids.

### D.10 Wing Cap: no inventory

1. Touching the block applies a Duration effect. It grants tag `Cap.Wing`
   (`replicate_to_all`) and the flight ability, whose hold is a movement-mode
   lease.
2. A tag presentation attaches the cap mesh.
3. When the duration ends, everything is withdrawn.
4. No item, container or slot is involved.

### D.11 Consuming the last unit (review point 7)

**Bottle of red potion in `use.y` (contents):**
1. **Press `Input.ItemY`.** The bottle's `use` grant has the bottle entry as
   its source. Admission is the start-cost `Check` of `{ item: { container:
   source, count: 1 } }`, which needs a contents entry.
2. **Drink phase** (`anim.hold` for 30 ticks), then a commit that consumes the
   potion and applies the heal, as one transaction.
   - The potion entry is depleted.
   - The bottle is untouched: still quantity 1, still assigned, still the
     source.
3. **Recover phase.** It ends normally.
4. **Reaper.** It destroys the potion entry. The bottle is now empty: no
   contents.
5. **Next press.** The `Check` fails, and the press is refused `Blocked` with
   the failing cost named. The HUD can read `inventory.count {container:
   bottle}` = 0.

**Bombs in `use.x` with `on_depleted: hold`, last bomb (quantity):**
1. The throw commit spends `{ item: source, 1 }`, and the stack is depleted.
2. The throw finishes.
3. The reaper waits for the activation, then applies `hold`: the record keeps
   `bombs` in `use.x` with no source, and the entry is destroyed.
4. Picking up bombs creates a new entry. The record rebinds to it, and the use
   grant returns.

**Automatic bow, ammo slot `release`, last arrow (quantity):**
1. The per-pulse `Check` fails at count 0. The activation ends `CommitFailed`,
   or continues with `on_fail: continue`.
2. After the activation ends, the reaper removes the ammo record and destroys
   the stack.

**Lantern with `Oil` charges:**
- Each pulse pays an effect cost on `source.Oil`.
- At 0 the lantern stays, unlit.
- An oil flask's use ability refills it with an effect on the lantern entry.

**Menu potion from a stack of 3 (transient grant, quantity):**
- The commit takes the stack to 2. Nothing is depleted.
- From a stack of 1, the stack is depleted. The transient grant ends with its
  activation, and the reaper then destroys the entry, which no record holds.

**Source destroyed by game code mid-activation:**
- `source` reads closed;
- the record is swept;
- the effect ends by owner liveness;
- the grant is revoked, and the activation ends `GrantRevoked` two ticks later.

### D.12 Two rings granting one spell (review point 1)

1. `ring_fire.sdata` and `ring_ember.sdata` each grant Fireball (key
   `g_fireball`) with `parameters.power` 10 and 14.
   - Their entries differ by origin, so they are distinct identities on every
     machine.
   - Before this change, `FindByWireKey(g_fireball, Fireball)` would bind the
     client's copy to whichever it interned first.
2. Equipped in `ring.left` (priority 20) and `ring.right` (priority 21).
   - A press starts the ember ring's activation (`param power` = 14, source =
     ember ring).
3. The ember ring is unequipped mid-cast.
   - Its activation ends `GrantRevoked`.
   - The next press starts the fire ring's activation.
4. If the layout gave both slots priority 20:
   - every press is `BindingAmbiguous`;
   - Kyusu's layout check and the dev equip warning name the tie.
5. Both rings' `SpellPower` effects fold additively, which is deliberate
   aggregation.

---

## E. Concrete data model (exploratory)

### E.1 Composition in source documents

```json
{
  "id": "5e0c…", "format_version": 2,
  "extends": "asset://prefabs/actors/guard.sscene",
  "includes": [ { "fragment": "asset://prefabs/fragments/heavy_armor.sscene" } ],
  "patch": {
    "11c04b1a77e0a001": {
      "Attributes": { "attributes": { "Health": { "base": 200 }, "Stealth": null } },
      "AbilityGrants": { "grants": { "g_shield_bash": { "ability": "asset://abilities/shield_bash.sdata" } } }
    }
  },
  "remove": { "11c04b1a77e0a001": [ "Alertable" ] }
}
```

- `extends` adopts the base's entities and ids. There is no wrapper.
- A fragment merges onto the root, or onto the entity named by `onto`.
- A keyed array is patched as a map from member key to patch, and `null`
  removes a member.
- Version-1 scenes keep whole-array replacement on read (D2).

```json
{ "id": "…", "type": "item.definition", "version": 1,
  "extends": "asset://items/armor.sdata",
  "includes": [ "asset://items/fragments/fire_enchant.sdata" ],
  "data": { "name": "Leather Tunic", "equip": { "effects": { "e_armor": { "scale": 14 } } } } }
```

A non-item consumer of the same envelope: an NPC variant of an ability.

```json
{ "type": "ability.definition", "version": 1,
  "extends": "asset://abilities/ctx_primary.sdata",
  "data": { "phases": { "p_fire": { "enter": { "o_damage": { "arguments": { "scale": { "const": 0.5 } } } } } } } }
```

### E.2 Item definitions and layout

```json
{ "type": "item.definition", "version": 1, "abstract": true,
  "data": { "name": "Clothing", "tags": [ "Item.Wearable" ], "stack": { "max": 1 },
            "world": "asset://prefabs/pickups/folded_cloth.sscene",
            "equip": { "occupies": [ "body.torso" ], "effects": [],
                       "present": [ { "key": "p_hide_torso", "lease": "body.sections.hide",
                                      "arguments": { "sections": { "const": [ "torso" ] } } } ] } } }
```

```json
{ "type": "item.definition", "version": 1,
  "data": { "name": "Fire Ring", "tags": [ "Item.Ring" ], "stack": { "max": 1 },
            "equip": { "occupies": [ "ring" ],
                       "effects": [ { "key": "e_power", "effect": { "data": "asset://effects/spell_power.sdata" }, "scale": 10 },
                                    { "key": "e_fireball", "effect": { "data": "asset://effects/grants/fireball.sdata" } } ] },
            "grants": { "g_fireball": { "parameters": { "power": 10 } } } } }
```

```json
{ "type": "item.definition", "version": 1,
  "data": { "name": "Pistol Ammo", "tags": [ "Item.Ammo" ], "keep": false,
            "take": { "effects": [ { "key": "t_ammo", "effect": { "data": "asset://effects/ammo_pistol_18.sdata" } } ] } } }
```

```json
{ "type": "equipment.layout", "version": 1,
  "data": {
    "slots": [
      { "key": "hand.right", "accepts": { "any": [ "Item.Wieldable" ] },
        "binding": { "action": "Input.PrimaryFire", "priority": 10 }, "socket": "hand_r", "order": 10 },
      { "key": "hand.left", "accepts": { "any": [ "Item.Wieldable", "Item.Shield" ] },
        "binding": { "action": "Input.AltFire", "priority": 10 }, "socket": "hand_l", "order": 10 },
      { "key": "ring.left",  "accepts": { "any": [ "Item.Ring" ] }, "binding": { "priority": 20 } },
      { "key": "ring.right", "accepts": { "any": [ "Item.Ring" ] }, "binding": { "priority": 21 } },
      { "key": "body.torso", "accepts": { "any": [ "Item.Wearable" ] }, "order": 20 },
      { "key": "body.cloak", "accepts": { "any": [ "Item.Cloak" ] }, "order": 30 },
      { "key": "use.y", "accepts": { "any": [ "Item.Usable" ] },
        "binding": { "action": "Input.ItemY" } },
      { "key": "use.x", "accepts": { "any": [ "Item.Usable" ] },
        "binding": { "action": "Input.ItemX" }, "on_depleted": "hold" }
    ],
    "lock_tags": [ "State.Attacking" ]
  } }
```

- An item that `occupies: ["ring"]` takes the first free slot whose key falls
  under the slot group `ring`. An explicit `slot` argument chooses one.
- A two-handed item lists `["hand.right", "hand.left"]` and becomes one record.

### E.3 Runtime components (sketch)

```cpp
struct ItemEntry       { PersistableRef Container; ItemId Definition; std::uint32_t Count; std::uint16_t Position; };
struct ItemContainer   { /* compiled rules index, capacity, accepted tag query */ };
struct ItemContainerRules { DataAssetHandle Rules; };          // authored, not replicated
struct ItemPickup      { PersistableRef Entry; ItemId Definition; std::uint32_t Count; };

struct EquipmentRecord { std::uint16_t Key; ItemId Definition; PersistableRef Source;
                         std::uint32_t Slots; std::uint8_t Primary; };
struct EquipmentState  { EquipmentRecord Records[kEquipmentRecordCapacity]; std::uint8_t Count;
                         std::uint16_t NextKey; };              // codec: definitions and slots to all, Source owner-only
struct EquipmentLayoutRef { DataAssetHandle Layout; };         // authored, not replicated

// AbilityKit
struct AbilityGrantEntry { AssetId Origin; std::string Key; AbilityId Ability; AbilityBindingDecl Override;
                           AbilityParameters Parameters; EntryOverlay Overlay; };
struct AbilityGrant      { /* existing */ EntityId Source; };  // wire: Source replaces the always-empty Effect
struct ActiveEffect      { /* existing */ std::uint16_t Overlay; };
// GameplayTagContainer: baseline stack count per tag beside the total (D9)
```

- **Container index.** A World resource.
  - The authority maintains it in inventory operations. There are no hooks,
    because replicated components may not have any (`ReplicationLayout.h:179-183`).
  - Clients rebuild the affected containers from `Changed<ItemEntry>`.
- **Instance state** is ordinary components on the entry. The engine ships
  generic ones only if two exercises need them.
- **Relevance.** An entry's `NetReplicated` and relevance follow its
  container's relevant peers (§C.3).

### E.4 Vocabulary

- **Verbs:**
  - `inventory.take`
  - `inventory.give {definition, count}`
  - `inventory.transfer`
  - `inventory.drop`
  - `inventory.use {entry}`
  - `equipment.equip`
  - `equipment.unequip`
  - `equipment.cycle {slot group}`
- **Cost provider:** `item` (`{ entry | slot, count }`).
- **Queries:**
  - `inventory.count {container, definition|tag}`
  - `inventory.has`
  - `equipment.in_slot {receiver, slot}`
  - `equipment.sources {receiver, tag}` (entities)
  - `equipment.definition`
- **Events:** `inventory.changed`, `equipment.changed`.
- **C++:** the same operations as functions on the `Inventory` and `Equipment`
  API objects that the verbs bind to.

---

## F. Cost and integration

### F.1 Cost model (estimates; Stage 0 measures)

| Scales with | Memory | Recurring CPU |
|---|---|---|
| Entries (all) | Row (~24 B `ItemEntry` + `PersistentIdComponent`) + registry slot + `PersistentEntityIndex` entry | None in gameplay |
| Replicated entries (relevant containers only) | + `NetReplicated`/`NetOwner` + change-store record (last published bytes per component, field-run generations) + a floor per peer | **Change-store walk + re-encode per publish (30 Hz)** |
| Unique instances | Their components | Only what game systems do |
| Equipped records | `EquipmentState` ≈ 16 × 20 B per equipped receiver; one `ActiveEffect` entity (~96 B) per maintained effect | Fold and grant sync per effect per tick (existing, A.4.7); reconcile on events (scan of all effects) |
| Actors without inventory | 0 | 0 |
| Definitions | Shared | Load-time composition only |

### F.2 Benchmarks (Stage 0, before any inventory code)

Stage 0 uses a stand-in component of the same size as `ItemEntry`, plus
`PersistentIdComponent`, plus `NetReplicated`/`NetOwner`. The point is to
measure **entities, not payloads**.

| Measure | Scenarios |
|---|---|
| Bytes per entry, broken down: chunk row, registry, `PersistentEntityIndex`, change store, per-peer floors | 1k / 10k / 50k entries; 0 / 25% / 100% replicated |
| Create, move-partition, destroy throughput | Batches of 1 / 100 / 10k |
| Authority publish time | Idle and 1%-changing replicated entries (0, 1k, 5k, 10k) × peers (1, 16) |
| Late-join seeding | One 300-entry inventory: snapshots and wall time to complete |
| Client container index rebuild | 300 entries changed vs 1 |
| Effect scans | `SyncEffectGrants` and fold at 600 / 2k / 5k Infinite effects, 0–4 grants each |
| Reconcile | One equip with 600 / 5k effects present |

Representative populations:
- Skyrim player: 300 entries.
- 200 NPCs × 8 entries, not replicated.
- 50 chests × 10.
- HL2 player: 12.
- 16 players × 150 replicated.

**Rules:**
- Results go in this document and in `abilitykit.md` "Costs".
- No numeric budget is set before the numbers exist.
- The owner reviews the results before Stage 4 commits to D3.
- No caching or change-store optimization before measurement.

### F.3 Integration

- **ECS.**
  - Engine components are registered before the first entity.
  - Entries live in their container's partition.
  - There are no new structural rules.
- **AbilityKit.**
  - Entry origin, overlay and parameters.
  - Grant `Source` and the `source` port.
  - `param` argument source.
  - Cost providers, and effect cost `target`.
  - Revoke-before-grant and reservations.
  - Request selection by priority, plus `source`.
  - Transient request grants.
  - `HasLiveActivationFrom`.
  - Override `precedence`.
  - **ABI and wire:** v29+. `kFrozenReplicationTableHash` moves.
- **Replication.**
  - Codecs for `ItemEntry` and `EquipmentState`.
  - Container-relevance replication.
  - Deferred entity-reference resolution.
  - The authority-materialized declaration, consulted by client import, net
    prefab instantiation and the snapshot writer. The applier is unchanged.
  - Per-entity relevance, whose loss feeds `OwedDestroys`.
- **Persistence.**
  - The persistable reference kind.
  - The runtime persistent-id mint.
  - Baseline tag stacks.
  - Records and entries persisted; effects reconciled.
- **Kyusu.**
  - Provenance on `PropertyDescriptor`, used by both sheets.
  - Effective-definition view.
  - Layout checks (priority and order ties).
  - Item refinement.
  - An equip preview on `AbilityPreviewSession`.

### F.4 Known versus speculative

**Known (read):**
- A.4.1–A.4.9;
- the whole-entity datagram rule;
- zone-only interest;
- the per-publish replication walk;
- the in-place mesh swap hazard.

**Speculative (needs Stage 0):**
- entry cost at scale;
- the effect-scan cost at equipment scale;
- the pawn first-send fit after the wire changes.

---

## Scenario exercises (RFC §14)

| Scenario | Authored | Runtime state | Executed by | Custom code |
|---|---|---|---|---|
| **Half-Life 2** | Weapon definitions with maintained grant effects; one `hand` slot; reserve ammo as attributes (as LF does); ammo pickups `keep: false` with `take.effects`; `Clip` attribute on weapon entries | Weapon entries; one record; attributes | Fire and reload (an atomic commit across actor and `source` effect costs); `equipment.cycle`; `inventory.drop` | Weapon-select HUD |
| **Skyrim** | Item hierarchy (`extends`); enchantments as fragments or instance components; ordered body slots; ring priorities | Entries with instance components; records; one effect set per record | Fold (with `precedence` where authored); grants with `param`; `source` reads enchantment; layered presentation; menu use via transient grant | Enchanting rules and UI |
| **Mario 64 Wing Cap** | One Duration effect, a flight ability, a tag presentation | One effect, one lease | Effects, movement lease, presentation | None |
| **Thief guard** | `humanoid → guard → elite_guard`; fragments; authored purse and sword entries plus a record | Entities, entries, records; effects for "alerted" and "enraged" | Perception, AbilityKit, inventory (`transfer` to pickpocket) | AI selection (Track A item 7) |
| **Zelda consumables** | Bottle definitions (an item container holding one `Item.Bottled` entry) with a `use` that consumes contents; bombs as quantity in `use.x` with `on_depleted: hold`; rupees as an attribute | Bottle entries with contents entries; bomb stack; records | Input-bound grants (predicted); item cost provider (`container: source` / `source`); reaper | None |
| **Custom item** | Game subtype or instance component, game verbs, optional game cost provider | Game components on entries | Game systems through the authored API | The mechanic only |

---

## G. Recommended stages

Stages 1–2 are a general engine feature and stand alone. Their gates use
non-item consumers.

**Readiness.** Stage 0 and Stages 1–2 are ready for implementation planning.
Each gets its own plan and review, separate from Stages 3–7.

**Reproduce first.** Every defect named in A.4 gets a failing test that fails
for the stated reason *before* its fix. The benchmark results are recorded and
reviewed before Stage 4 commits to entity-per-entry storage (D3).

0. **Prerequisites and measurement.**
   - Failing test plus fix for nested-instance spawn (A.4.1).
   - Failing test plus fix for grant wire identity collision (A.4.2). This is
     a live defect independent of items; its content-qualified identity is
     the first slice of §B.2.
   - Revoke-before-grant, plus traced sync failure (A.4.3, A.4.4).
   - The F.2 benchmarks.
   - Stale doc fix.
   - *Gate:* tests green; benchmark results recorded and reviewed.
1. **One composition operator.**
   - Core operator: keyed arrays, `null`, conflicts, provenance.
   - Scene composition consumes it, with version-1 compatibility fixtures.
   - Data-asset `extends`/`includes`/`abstract` with closure reload.
   - Fact-schema migrated onto it.
   - *Gate:* table-driven merge tests; old-format fixtures load unchanged; an
     ability variant and a movement-profile variant load.
2. **Prefab `extends` and fragments.**
   - No wrapper; R1–R5; `removes_owed`; recook dependents.
   - Provenance, revert and conflict fix verbs in both sheets.
   - *Gate:* the guard chain cooks, spawns and inspects, with no item code.
3. **AbilityKit source, parameters, costs, precedence.**
   - §B.2, §B.3, B.5 reservations, B.6 `precedence`, transient request
     grants.
   - Pawn first-send fit test.
   - *Gate:* the two-rings scenario (D.12), and arrow-plus-stamina atomicity,
     as runtime and replication tests.
4. **Identity and inventory.**
   - Persistable reference; runtime persistent-id mint, which exists only where
     the simulation is authoritative; deferred entity-reference resolution.
   - Authority-materialized declaration in its three consumers. A failing test
     comes first for A.4.6.
   - **Validation requirements for the Stage 4 plan.** These are not a
     redesign; each needs a test:
     - prefab members that clients skip, including the group bookkeeping
       (`SceneInstance`, `Root`, `Entities`);
     - parent/child relationships that cross the skip: a skipped member with
       children, and a kept member whose parent was skipped;
     - per-peer relevance loss and regain, through `OwedDestroys`;
     - an authority-materialized entity as the prefab **root**, where today a
       net spawn binds to the root (`NetPrefabSpawner::Instantiate`, `SingleRoot`).
   - Per-entity relevance feeding owed destroys. It skips irrelevant entities
     before per-peer encoding (D3 condition 2), with tests for ownership
     transfer, late join, relevance loss and recovery, and despawn. It is
     benchmarked against the Stage 0 baseline before inventory replication is
     called production-ready (D3 condition 3).
   - Item registry; entries, containers, pickups, index; `MoveEntry`; reaper.
   - Container-relevance replication; item cost provider.
   - *Gate:* pickup, stack, split, transfer, drop, consume, depletion. The
     chest zone round trip (D.9.2). A two-instance net test with a late joiner.
5. **Equipment.**
   - Layouts, records, `ReconcileEquipment`, the full check, `lock_tags`,
     `on_depleted`.
   - Baseline tag stacks (D9), with a test per row of the D9 audit. In
     particular:
     - a replica-decoded container saves to nothing;
     - a restored character in an active movement mode carries the mode's
       tag with no transition.
   - *Gate:* the HL2, Skyrim and Zelda exercises (bottle contents, `hold`
     bombs); unequip mid-ability; the serialize, restore and reconcile identity
     test (§B.4).
6. **Presentation.**
   - Extract the sustained diff; equipment and tag sources.
   - Body leases ordered by slot; retained mesh/material swap.
   - Joint query and sockets.
   - *Gate:* golden images; Wing Cap on a remote peer.
7. **Kyusu item authoring.**
   - Item refinement, effective view, layout checks, equip preview.
   - *Gate:* owner walkthrough.

**Later, each with a named owner:**
- viewmodel pass (Track B);
- the save system and zone overlay (Track A item 8 / Track C item 5); entries
  are ready for them;
- predicted equip;
- observer relevance for opened chests;
- `on_equip` one-shots, once a consumer appears.

---

## H. Decisions

| # | Decision | Status | Preferred | Consequence |
|---|---|---|---|---|
| D1 | No prototype asset kind; compose prefabs and data assets | **Approved in principle** | One core operator; contributed classification tags | No runtime lineage |
| D2 | Keyed collection merge with a scene format version | **Approved, with compatibility tests** | v2 writes keyed maps; v1 reads as whole-array replacement | Migration fixtures |
| D3 | Entries are entities, one per stack or unique instance | **Approved** (2026-10-15, after the Stage 0 measurements in `docs/plans/evidence/entry-entities/results.md`) | As stated. Conditions: (1) an entry is a passive entity, with no transform, spatial presence, renderer, physics or per-tick inventory processing; (2) per-entity relevance skips irrelevant entries before per-peer encoding, tested across ownership transfer, late join, relevance loss and recovery, and despawn; (3) that filter is benchmarked against the Stage 0 baseline before inventory replication is called production-ready, since its savings are projected, not measured; (4) schemas, field codecs and value types stay independent of ECS storage. | The change store's per-publish copy is a separate replication item (`docs/deferred.md`), not a dependency of this work. |
| D4 | Contributions never structurally mutate the receiver | **Approved** | Effects, leases, contribution entities; no component reference counting | Revisit only with a real counterexample |
| D5 | Grant identity, source and selection | **Approved in principle** | Content-qualified entry identity; `source` port; definition-level `parameters`; selection by explicit priority; no collapse; aggregation through effects; deferred reference resolution | ABI and wire bump, gated by the wire, ABI and pawn first-send tests; fixes A.4.2 |
| D6 | Authority-only mutation in v1 | **Approved for v1** | Item use through input-bound grants is predicted; the swap is presented at once | One round trip before a new weapon fires remotely |
| D7 | Conflicts and precedence | **Approved** | Static ambiguity is an error resolved by a local value; runtime `precedence` on Override; presentation `order`; input `priority`; ties diagnosed | Default precedence keeps today's behaviour |
| D8 | Dropped entries | **Approved as a temporary world-persistence limitation** | Identity and references are overlay-ready | A persistent drop waits for the overlay, not a redesign |
| D9 | Tag persistence | **Approved, with the audit (§B.4)** | A baseline stack count per tag; a split API; replica decode writes contributions only | +64 B per tag container; ABI bump |
| D10 | Ability cost transactions | **Approved** | Registered cost providers: read-only batch `Check`, infallible `Apply`, one owner per resource (item provider: entry quantities; effect provider: attributes); effect costs gain `target`; no transactional verbs | A narrow, testable extension |
| D11 | Quantity, charges, contents | **Approved in principle; revised in revision 3** | Quantity 0 is a transient depleted state, always reaped once nothing runs from it. Charges are entry attributes; the item persists at 0. Contents are entries inside a container item. Slot `on_depleted`: `release` or `hold`. | An empty bottle is a bottle; an exhausted arrow stack disappears after its last shot |
| D12 | Client materialization of private entries | **Revised in revision 3** (revision 2 rejected) | Component types may declare entities authority-materialized: clients skip them on content import and net prefab instantiation, and the authority sends no authored identity for them. They exist on clients only as replicas while relevant, and leave through `OwedDestroys`. | The snapshot applier and its duplicate refusal are unchanged; one registration flag and three predicates |
