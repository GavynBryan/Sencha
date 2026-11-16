# Composition Stage 3: grant source, parameters, costs and precedence

Stage 3 of `docs/plans/definition-composition-inventory-equipment.md` (§G.3).
It covers §B.2 (grant identity, source and selection), §B.3 (cost providers),
the grant reservations of §B.5, the `precedence` of §B.6, and the transient
request grants of §B.7. It adds no item or equipment code. Where the gate
needs one, a stand-in is named below.

**Status on main.** S3.1–S3.9 are ported, with the Stage 2–4 audit's AbilityKit
fixes (AK-B1: admission reads the grant it admits, and a start that still cannot
pay ends `CommitFailed`; AK-S1: `ability.grant` and transient grants leave the
room effect reservations hold; AK-S3 landed with Stage 0) and the audit's delta
fix to late-bound references (a delta lands on the image that still waits). The
cost providers carry the Stage 5 remediation's translation (`SpendsThrough`,
`Translate`: R1), tested with AbilityKit-only providers until inventory's item
provider arrives. On main, running activations live in the World's
`AbilityActivations`, so `HasLiveActivationFrom` and the transient sweep read
that pool, and an effect application's `EffectSource` (who caused it) stays a
separate thing from a grant's `Source` (what the grant came from).
`NamedDefinitionRegistry` waits for the inventory stage, its first sharer. The
ABI changes are recorded under v48; renaming the grants codec's per-grant run
moves the replication table hash, and the protocol stays 9.

## What Stage 3 delivers

- **Source.** A grant names the entity it came from, and an ability reads it
  through the `source` port.
- **Selection.** A request picks the grant with the highest priority, then the
  lowest grant id. An optional `source` argument names the grant exactly.
- **Late-bound references.** An entity reference that arrives before its
  entity is applied again when that entity binds (A.2.10).
- **Parameters.** A grant entry carries per-definition numbers, and an ability
  reads them with a `param` value.
- **Precedence.** An `Override` modifier has an authored `precedence`. The
  highest wins, and a tie is reported.
- **Cost providers.** A cost group can name registered resource providers
  beside its effects. An effect cost can be charged to the actor or the
  source. Check and apply stay one transaction.
- **Reservations.** Applying a grant-bearing effect reserves room in the
  target's grant table, so the grant pass cannot fail for it.
- **Transient grants.** A request can activate an entry that the actor is not
  granted. The grant lives exactly as long as that activation.

## Invariants

1. **Content identifies an entry.** Two entries that differ in key, binding or
   parameters are different entries on every machine. Two entries with equal
   content are one entry, and the grants that use it stay distinct by grant
   id and source.
2. **An activation has exactly one source.** It is the source of the grant it
   ran through, and it does not change while the activation lives.
3. **A commit is one transaction.** Every check passes, then every apply runs,
   on the owner thread with nothing in between. An apply cannot fail.
4. **Effects know nothing about abilities.** The reservation reaches the
   effects layer through a narrow admission hook that AbilityKit installs.
5. **Defaults keep today's behaviour.**
   - Precedence 0 everywhere folds exactly as today.
   - A cost group with no resources and an actor target pays exactly as today.
   - A grant with no parameters keys exactly as today.

## Decisions and named deviations

1. **No `origin` field; parameters join the content key.**
   - RFC §B.2 names identity `{origin, key, overlay}`. Stage 0 (S0.3) already
     replaced that with a 32-bit content key over key and binding, for two
     reasons: scene loading knows no asset path for authored grants, and the
     pawn seed had no room for more words.
   - Stage 3 adds parameters to that key.
   - D.12 still holds:
     - two rings with powers 10 and 14 are two entries;
     - two rings with equal parameters are one entry, and their two grants
       differ by grant id and by source.
   - A client therefore resolves every grant from content and never needs the
     origin asset. Origin is provenance, and Kyusu shows it from composition.
2. **The overlay waits for layouts.**
   - A slot's binding overlay (§B.2) needs `equipment.layout`, which is
     Stage 5.
   - Stage 3 keeps the overlay as content: Stage 5 interns the entry with the
     slot's binding merged into its override, so the key covers it with no
     new field.
3. **`Holder` and `Source` are two fields.**
   - `AbilityGrant::Effect` becomes `Holder`. It is the effect entity whose
     life bounds an effect grant. It stays on the authority and is never sent.
   - `Source` is new. It is what the ability reads:
     - an effect grant's source is the holding effect's `LeaseOwner` entity;
     - a request grant's source is the `source` argument;
     - an authored grant has no source.
   - The wire's existing per-grant u64 run carries `Source` in place of
     `Effect`. It is renamed `sources`. That costs no bytes, but the frozen
     table hash moves and `kNetProtocolVersion` goes to 9.
4. **Transient grants are their own kind.**
   - RFC §B.7 says Kind `Request`. A flag beside `Request` would be lifecycle
     state stored as a bool.
   - So `AbilityGrantKind::Transient` names a grant that ends with its one
     activation.
5. **The reservation hook is a seam at a layer boundary.**
   - `effects/` declares `EffectApplicationAdmission` (a World resource made
     of two function pointers and a context). `ApplyEffect` asks it before
     spawning a lasting effect and tells it afterwards.
   - AbilityKit installs it. A World without AbilityKit applies effects as
     today.
6. **Cost providers mirror the candidate catalogs.**
   - They live in an `AuthoredCatalog` whose definitions carry `Check` and
     `Apply` function pointers.
   - They are declared through a registration scope.
   - An ability resolves them by name, through `AbilityResolveEnvironment`.
7. **Gate stand-ins.**
   - **Two rings:** two source entities, each holding one effect that grants
     Fireball. One has priority 20 and power 10, the other priority 21 and
     power 14.
   - **Arrows:** a test-registered `test.counter` provider over a test
     component, which stands in for the item provider of Stage 4.

## Steps

Each step starts from a failing test, ends with a commit, and runs the full
`ctest --preset dev`.

### S3.1 Grant source and the `source` port

- `AbilityGrant` gains `Holder` (renamed from `Effect`) and `Source`.
- `GrantAbility(grants, ability, kind, entry, holder, source)`.
- Effect grants take `Source` from `ActiveEffect::Owner.Entity`.
- `ability.grant` gains an optional `source` entity argument.
- `AbilityPort::Source` (authored `source`) is entity-shaped. It reads the
  record's grant's source. An entity argument from `source` needs an `entity`
  guard, like a local or `target`.
- `HasLiveActivationFrom(const World&, EntityId actor, EntityId source)` is for
  Stage 5's unequip path.
- Wire: `sources` replaces `effects`; protocol version 9.
- Tests:
  - an effect grant's ability reads its holder's owner as `source`;
  - a client's grant carries the authority's source;
  - an unguarded `source` recipient is refused at declaration;
  - `HasLiveActivationFrom` answers per source.

### S3.2 Requests choose by priority, or by source

- `AbilityRequest` gains `Source`. `ability.activate` gains an optional
  `source` argument.
- Selection takes the highest effective priority, then the lowest grant id.
  With a source, only grants from that source are candidates.
- An entry's effective priority is resolved even when it binds no action.
- Tests:
  - two grants of one ability: the request takes the higher priority, then the
    lower id;
  - a request naming a source takes that grant;
  - a source that holds no matching grant is refused with `NotGranted`.

### S3.3 Late-bound entity references (A.2.10)

- The client applier remembers, per entity, the components whose decode met
  an unresolved `NetEntityId`, together with their last wire image. The list
  is bounded.
- When that id binds, it applies those images again.
- Consumers today: grant `Source`, and `AnimRequestSet` entity fields.
- A re-application must not re-fire an impulse. That is checked against
  `AnimRequestSet`'s `Observed` reset.
- Tests:
  - a grant whose source arrives a snapshot later reads it once it binds;
  - the list is bounded and forgets on despawn.

### S3.4 Grant parameters and the `param` value

- An ability declares `parameters: { name: { default } }` (numbers).
- A grant declares `parameters: { name: value }` in the scene form, the
  effect-grant form and `ability.grant`. A name the ability does not declare
  is a diagnostic where the grant is declared.
- `AbilityGrantEntry::Parameters` is a sorted list of name and value. It joins
  the content key, written exactly.
- `Refresh` resolves each entry's parameters to a dense array in the
  ability's declared order, so the pass never looks a name up.
- `AbilityValueSource::Parameter` (authored `param`).
- Tests:
  - two entries that differ only in a parameter are two entries on both
    machines;
  - an ability reads the grant's value, else its default;
  - an undeclared parameter is reported.

### S3.5 Override precedence

- `EffectModifier::Precedence` (int32, default 0), authored `precedence`.
- In the Override phase of the fold, the highest precedence per target and
  attribute wins. Ties keep today's canonical order.
- Each tie is recorded once per attribute and pair of definitions in an
  `EffectOverrideTies` World resource. A dev log reads it, and so will Kyusu's
  equip preview.
- Tests:
  - `Frozen` (precedence 100) beats `Sprint Boots` (precedence 10) in either
    application order;
  - a tie is recorded once;
  - precedence 0 folds as today.

### S3.6 Cost providers and effect cost targets

- Cost group: `target: actor | source` (default `actor`) for its effects, and
  `resources: [{ key, provider, arguments }]`.
- `AbilityCostProviderDefinition`:
  - `Name` and `Arguments`;
  - `Check(const World&, AbilityCostBatch) -> AbilityCostCheck`;
  - `Apply(World&, AbilityCostBatch)`.
- A batch holds each row's actor, source and evaluated arguments.
- `CheckEffectCosts` sums per `(target, attribute)` over rows of target and
  effect.
- The commit gathers every payable group's effects and resources, and makes
  one check per provider with its whole batch, plus one effect check. Only
  then does it apply: effects first, then providers in definition order.
- A failed commit traces `CostFailed` with the failing group. For an
  `Insufficient` effect cost, the check reports which effect failed.
- A guess checks and never applies. When the authority refuses an activation
  that a guess started, the guess ends `AuthorityDenied` (read and fixed
  here).
- Tests:
  - one provider batch sums two groups' rows;
  - a failing provider applies no effect cost, and a failing effect cost runs
    no provider;
  - a source-targeted effect cost charges the source;
  - an unknown provider and bad arguments are refused at declaration.

### S3.7 Grant reservations

- `AbilityGrantReservations` is a World resource holding target, holding
  effect and count.
- AbilityKit's admission refuses a grant-bearing effect when the target's
  live grants plus its reservations would exceed `kAbilityGrantCapacity`.
  Grants whose holder is dead count as free.
- `SyncEffectGrants` drops a reservation once its effect's grants are held or
  the effect is gone.
- Tests:
  - two effects applied in one tick into a table with room for one: the
    second is refused at apply, and the first's grant always lands;
  - a reservation clears when its effect ends before the pass.

### S3.8 Transient request grants

- `RequestTransientActivation(world, actor, entry, source, instigator)`
  queues a request of kind `ActivateEntry`.
- The pass grants it as `Transient` and starts it.
  - A refused start revokes it on the spot.
  - An activation that ends revokes its transient grant in the same `End`.
- Tests:
  - a transient grant exists only while its activation runs;
  - a refused one leaves no grant;
  - the client sees it end with the activation.

### S3.9 Gate and documents

- `test/runtime/AbilitySourceGateTests.cpp` (runtime):
  - **Two rings (D.12).** A press starts the ember ring's activation, with
    power 14 and source ember. Removing that holder ends the activation
    `GrantRevoked`, and the next press starts the fire ring's, with power 10.
    With equal priorities the press is `BindingAmbiguous`.
  - **Arrow plus stamina.** A shot costs one counter (arrow) and 10 stamina.
    - With no arrows, stamina is untouched.
    - With no stamina, arrows are untouched.
    - With both, both are paid on one tick.
- `test/runtime/AbilityReplicationTests.cpp` (replication): the two rings on
  an owner client resolve to the authority's entries and sources, and the
  shipping pawn's first send still fits.
- Documents:
  - `docs/gameplay/abilitykit.md` (source, selection, parameters, providers,
    transient grants, reservations);
  - the effects doc's precedence section;
  - `docs/architecture/module-abi-history.md`;
  - `docs/deferred.md`, for anything left.

## Compatibility

- **ABI.** `AbilityGrant`, `AbilityRequest`, `EffectModifier` and
  `AbilityCostGroup` change layout. On main these are recorded under v48.
- **Wire.** The frozen hash moves; the protocol is 9 on main already.
- **Formats.** All are additive. Missing fields read as today's behaviour,
  and nothing is migrated.

## Deviations from this plan, named during implementation

1. **S3.3: the waiting images live in the client identity.**
   `ReplicationClientIdentity` holds the `ReplicationDeferredReferences`, so
   every applier gets late binding with no new request field. The
   grant-specific test is replaced by the codec-generic net test plus the
   `AnimRequestSet` test, since the grants codec resolves through the same
   `LocalEntity`.
2. **S3.4: grant parameters are a keyed list.**
   - RFC §E sketches `"parameters": { "power": 10 }`. The scene archive cannot
     enumerate an object's members, so the form is
     `[ { "key": "power", "value": 10 } ]` everywhere.
   - In a scene, the grant's parameters merge by key when a prefab extends
     another.
3. **S3.4: an undeclared parameter is reported when bindings resolve,** not
   where the grant is declared.
   - A scene names an ability before anything declares it, so the declaration
     site cannot know the ability's parameters.
   - The report is the same once-per-rebuild warning an unknown action gets.
4. **S3.4: `ability.grant` takes no parameters.** A verb argument cannot carry a
   keyed list. The verb's known consumers (scripts and tests) grant parameterless
   entries, and an item supplies its parameters through its effect's grant
   entries.
5. **S3.6: a refused guess is forgotten, not ended `AuthorityDenied`.**
   - RFC §B.3 says a stale guess "ends `AuthorityDenied` as today". Today the
     journal forgets a guess the authority started nothing for, and takes down
     its presentation (`RefusedPredictionEndsSustainedPresentation`). Nothing
     assigns `AuthorityDenied`.
   - The behaviour is already the one the RFC wants: the authority decides, and
     the guess leaves no trace. Stage 3 keeps it and does not invent an end
     reason no path reaches.
6. **S3.6: a failed commit adds no new trace event.** It still ends
   `CommitFailed` at its op. `AbilityCommitResult::Group` now names the failing
   group: an insufficient effect cost reports its effect, and a provider reports
   its row.
7. **S3.6: providers come from the World's catalog, and a game declares its
   own from `Game::OnRegisterVocabulary(World&)`.** No new module hook is
   needed. The Ability Workbench shows a resource's provider and its arguments
   as plain data; contract-driven argument rows like an op's wait for the first
   content provider (`docs/deferred.md`).
8. **S3.8: a transient grant is taken back by the next pass's sweep,** not
   inside the `End` that ended its activation. An `End` can run while a caller
   walks the grant table (an op cancelling another ability during a press), and
   a revoke there would shift the table under it. The grant outlives its
   activation by at most one tick. The request kind is `ActivateEntry`; the
   inventory verb that issues it comes with Stage 4.
9. **S3.9: the gate lives in the existing fixtures,** not in a new
   `AbilitySourceGateTests.cpp`. The runtime and replication fixtures are local
   to their files, and a third copy of either would be a duplicate. The two-rings
   tests are `TwoRings.*` in `test/runtime/AbilityRuntimeTests.cpp`. The
   arrow-plus-stamina runtime test is
   `AbilityRuntime.AnArrowAndStaminaArePaidTogetherOrNotAtAll`. Their replication
   halves are in `test/runtime/AbilityReplicationTests.cpp`. The test provider
   is shared through `test/support/CounterCostProvider.h`.

## Gate result

- **Two rings (D.12), runtime.**
  - A press casts from the ember ring (power 14, source ember).
  - Taking it off ends that activation `GrantRevoked`.
  - The next press casts from the fire ring (power 10).
  - Tied priorities cast nothing and trace `BindingAmbiguous`.
- **Two rings, replication.** On the owner, each grant resolves to its own
  entry (power 10 or 14) and to the client's copy of its ring.
- **Arrow plus stamina, runtime.**
  - With no arrow, stamina is untouched.
  - With no stamina, the arrows are untouched.
  - With both, both are paid on one tick.
  - One provider batch covers both groups' rows.
- **Arrow plus stamina, replication.** A guess checks both costs and pays
  neither. The authority pays both, and its stamina arrives at the client.
- **Pawn first send.** The shipping pawn's owner seed still fits. The full
  pawn's is 1,129 of 1,173 bytes (1,117 before Stage 3).
