# T: the Sencha gameplay scripting language (v1.0 design)

Status: design accepted, not yet implemented. This document is the evaluation and
the v1.0 specification baseline for Sencha's scripting layer. The roadmap entry
lives in `real-engine-roadmap.md` (Phase 2.5).

The language is named T (pronounced "Tea"; it rhymes with C). Source files are
`.t`, cooked bytecode is `.tbc`. Engine-side identifiers do not use a `T` prefix:
it collides with the template-parameter convention in C++ and the retired tea
theme stays out of engine identifiers. The engine subsystem uses the mechanical
register: `ScriptCompiler`, `ScriptVm`, `ScriptModule`, `ScriptAssetLoader`,
`ScriptHostApiTable`, `ScriptBehaviorSystem`.

## Summary and recommendation

Sencha should build T rather than embed a Lua-family VM.

T is a small, statically typed, non-object-oriented gameplay language compiled at
cook time to deterministic register bytecode and executed by a small interpreter
inside scheduled fixed-tick systems. It is asset-native (cooks, caches, hot
reloads through the existing pipeline), reflection-integrated (script components
register through the same `TypeSchema` / `ComponentTypeId` path as native
components), and constrained to explicit gameplay seams (tags, AbilityKit,
CommandBuffer, physics queries, movement intents, cues).

The reasoning, in order of weight:

1. The hard constraints (enforced determinism, cook-time static validation,
   script components in the native reflection path, instruction budgets, a
   closed host surface, no GC-driven variance) are exactly what off-the-shelf
   VMs do not provide. Embedding Lua means maintaining a determinism-audited
   fork (stripped stdlib, controlled table iteration, pcall policy, GC tuning)
   plus a hand-written typed binding layer, and still surrenders cook-time type
   checking and inspector-grade component reflection.
2. The custom cost is real (compiler, VM, tooling: roughly 10-15k lines of C++)
   but bounded, because v1.0 T is deliberately tiny: no classes, no closures, no
   generics, no dynamic containers, no script-side heap.
3. The load-bearing design decision: **T has no persistent VM state. All
   persistent state is component data plus a current-state name.** No script
   heap, no coroutine stacks, no static locals. That one rule makes hot-reload
   migration, determinism, replay, and save/load tractable, and it is why
   coroutines are deferred.

Syntax direction: the Sencha-native hybrid (Option D below), not a clone of C,
Rust, or Go.

Repo grounding: the in-repo roadmap contains no scripting item and no Lua
recommendation (the Lua-family note lived outside this repository). The only
existing hooks are the reserved `AssetType::Script = 7` value in
`engine/include/core/assets/AssetRef.h` and the deferral sentence in
`docs/assets/pipeline.md` Decision F. This design fills that reserved slot; it
replaces nothing in-tree.

## Product role

T sits between data and native modules:

1. Data configures systems (manifests, `.smat`/`.stex`, cvars, tags, ability
   definitions, component values).
2. T scripts customize entity behavior, abilities, interactions, triggers, and
   small gameplay glue.
3. Native game modules implement systems-heavy code (movement, physics,
   animation, rendering, AI planners, anything with queries over many entities).

T is not a general-purpose language. For v1.0 there are no script-defined ECS
systems. T attaches behavior to native extension points owned by scheduled
systems:

- ability callbacks (AbilityKit),
- entity behavior callbacks (`ScriptBehaviorSystem`),
- trigger volume callbacks (`TriggerVolumeSystem`, new),
- interaction callbacks (`InteractionSystem`, new),
- all of the above on the fixed tick.

The script author sees `Entity`, components, tags, assets, contexts, physics
queries, movement requests, commands, and reflected fields. The author never
sees archetypes, chunks, registry internals, worker lanes, or scheduler
machinery.

## Design principles

1. If data can express it, prefer data over T. (CLAUDE.md prime directive 3
   restated; it comes first on purpose.)
2. Gameplay-first, not general-purpose.
3. Static by default: every value has a type known at cook time.
4. No hidden engine access: the host API table is the entire world.
5. No raw ECS: no queries, no registry mutation, no archetype visibility.
6. No object hierarchy: records and functions, not classes.
7. Scripts are assets: cook, cache, hot reload like any other asset type.
8. Components are records; script components are real components.
9. State is explicit: `state` declarations and `enter`, never hidden flags.
10. Host APIs are declared and versioned.
11. Determinism is enforced by construction, not documented.
12. Everything script-visible is inspectable or cook-validatable.
13. Native modules remain the path for systems-heavy code.
14. The complete v1.0 feature set is teachable in one short document.
15. No persistent VM state: all persistence is component data plus the current
    state name. No script heap, no suspended stacks, no static locals.

## Syntax direction: evaluation

Four directions were compared against: readability for gameplay code,
friendliness to technical designers, strength of static analysis, ease of
deterministic bytecode compilation, editor tooling, error-message quality, fit
to ECS/reflection/components, avoidance of OO inheritance, expression of
ability/entity/trigger/interaction behavior, small state machines, and v1.0
implementation cost.

**A: C / QuakeC-like.** Familiar and imperative, with the right lineage, and it
maps trivially to register bytecode. But it imports expectations T must refuse:
pointers, manual memory, classes-by-way-of-C++, and `goto`. Every feature a
C-shaped language conspicuously lacks reads as a missing feature rather than a
design choice. `void f(T x)` declarations also parse worse (no leading keyword)
and produce weaker error recovery than keyword-led declarations.

**B: Rust-like.** Strong on static typing, records, and non-OO defaults, and
`fn`/`let`/`name: Type` are genuinely good for tooling and error messages. But
full Rust syntax implies ownership, borrowing, lifetimes, traits, and macros.
T wants none of those (entity and component access is VM-mediated through the
engine, not memory borrowing), and the missing features would read as broken
Rust. Designer-intimidation is real.

**C: Go-like.** Minimal and approachable, `:=` is pleasant, easy to compile.
But Go syntax implies goroutines, channels, interfaces, and packages, all of
which T refuses; the capitalization-is-export convention is noise in a language
with no visibility model; and `var x Type` postfix types are the only thing the
rest of the syntax would take from Go anyway.

**D: Sencha-native hybrid (chosen).** Take the pieces that serve the criteria
and nothing that drags obligations:

- `fn`, `let`, `name: Type` annotations (Rust): keyword-led declarations parse
  and recover well, and annotations read left-to-right for designers.
- Brace blocks and C-family operators (`&&`, `||`, `!`, `==`): universal.
- Minimal surface and newline-terminated statements (Go).
- Gameplay-state flavor (QuakeC): first-class `state` and per-state callbacks,
  with `enter` instead of `goto`.
- Sencha-native literals: `tag"hookshot.anchor"`, `cue"door.open"`,
  `prefab"props/coin"`. These are typed, cook-validated references, not
  strings.

The hybrid reads as its own small language, which correctly signals "this is
not C++; do not expect C++ features." That signal is worth more than any single
familiarity win from options A-C.

## The syntax questions, answered

1. **Semicolons?** No. Statements are newline-terminated; a statement continues
   when the line ends inside an open bracket or after an operator or comma. A
   `;` is accepted as a separator but never required, so C-habituated authors
   pay nothing.
2. **`fn`, `func`, or C-style?** `fn`. Shortest keyword-led form; keyword-led
   declarations give better parse recovery and error messages than C-style.
3. **`let x: Type`, `var x Type`, or `Type x`?** `let x: Type = expr`, or
   `let x = expr` with inference. Locals are mutable; an immutable-by-default
   rule fights gameplay code for no analysis win at this scale.
4. **Local type inference?** Yes, locals only. Function signatures, component
   fields, `const`, and `param` declarations are fully annotated. Inference
   never crosses a declaration boundary, so every cook error stays local.
5. **First-class `state`?** Yes, inside `ability` and `behavior` blocks.
   Callbacks bind per state (`fn Pulling.fixed(ctx)`), with optional
   `fn Pulling.enter(ctx)` and `fn Pulling.exit(ctx)` hooks.
6. **Transition form?** `enter Pulling`. The transition takes effect after the
   current callback returns: no re-entrancy, deterministic ordering, and `exit`
   / `enter` hooks run in a defined order at the boundary. No `goto`.
7. **Pattern matching?** Not in v1.0. Enums plus `if` / `else if` cover the
   target use cases; `match` is a v2.0 candidate once enums prove out.
8. **`Option<T>` / `Result<T>` or validity structs?** Validity structs. Host
   queries return records with a `valid` field (`hit.valid`); reading a
   component that is absent is a deterministic trap, guarded by
   `entity.has(Component)`. Simpler for designers, and traps are well-defined
   (see Debugging).
9. **User enums?** Yes. C-like, `i32`-backed, allowed as component fields.
10. **Generics?** No.
11. **Closures/lambdas?** No. No function values at all in v1.0.
12. **Arrays/lists?** Fixed-length arrays only: `[T; N]`, in components and
    locals. Keeps components trivially copyable PODs and the VM heap-free. No
    dynamic lists.
13. **Maps?** No.
14. **Functions across files?** Yes, minimally: free functions, `component`,
    and `enum` declarations are importable.
15. **Modules/imports?** `import "scripts/lib/steering.t"`. Path-based,
    acyclic, no namespaces, no package system. The cook hashes the import
    closure, so editing a library invalidates its dependents.
16. **Script-defined components in v1.0?** Yes; the hookshot workflow requires
    them. Fields are restricted to `bool`, `i32`, `i64`, `f32`, `f64`, `Vec2`,
    `Vec3`, `Quat`, `Entity`, `TagId`, enums, `[T; N]`, and typed asset refs.
    A `component` declaration compiles to a synthesized `RuntimeField` table
    plus `ComponentTypeId = MakeComponentTypeId("script.<Name>")`, registered
    through a non-template `ScriptComponentSerializer : IComponentSerializer`,
    so script components appear in the inspector and participate in save, cook,
    and hot reload exactly like native components.
17. **Hot reload state migration?** Because persistent state is only component
    data plus a state name, migration is mechanical. Schema hash unchanged:
    keep component bytes as-is. Schema changed: match fields by name and type
    (keep matches, default new fields, drop removed ones). Active state
    machines re-enter by state name; a state that no longer exists
    deterministically cancels that ability or behavior instance. The bytecode
    itself swaps via the existing `ReloadInPlace` slot swap: handles never
    change.
18. **Runtime errors in PIE?** A trap (missing component, dead entity, index
    out of bounds, budget exhausted, bad host argument) deterministically
    aborts the callback with cancel semantics, logs one structured error with
    script and line via the source line table, surfaces in the editor console,
    and badges the entity. Cooked builds keep the same semantics and log.
    Traps are part of the deterministic contract, not exceptions: the same
    inputs trap at the same instruction every run.
19. **Deterministic replay?** Script execution is a pure function of component
    state, `TickIndex`, fixed `dt`, the seeded RNG stream, and host query
    results. Replay is therefore the engine's input replay; scripts add no
    state of their own. A cvar-gated per-tick checksum (instruction count plus
    a hash of component writes) gives the determinism gate a cheap divergence
    detector.
20. **Deferred to v2.0?** See Non-goals and the v2.0 list at the end.

## Language reference sketch (v1.0)

### Types

`bool`, `i32`, `i64`, `f32`, `f64`, `Vec2`, `Vec3`, `Quat`, `Entity`, `TagId`,
user enums, fixed arrays `[T; N]`, typed asset refs (`CueRef`, `PrefabRef`,
`AssetRef`), and component record types. Numeric conversions are explicit
(`f32(x)`, `i32(x)`); there are no implicit narrowing conversions. Strings
exist only as compile-time literals (tag/cue/asset literals and `log` format
strings); there is no runtime string type.

### Declarations

- `component Name { field: Type = default ... }`
- `enum Name { A, B, C }`
- `ability Name { ... }`, `behavior Name { ... }`, `trigger Name { ... }`,
  `interaction Name { ... }`
- `state Name` (inside ability/behavior blocks)
- `fn name(args) -> Type { ... }` and per-state `fn State.name(args) { ... }`
- `const name: Type = literal` (compile-time constant)
- `param name: Type = literal` (a tunable: the default lives in the script, and
  the owning asset, for example a `.sability` file, may override it as data.
  Cook validates overrides by name and type. This keeps tuning in data,
  directive 3, without a recompile.)
- `import "path.t"`

Fixed callback names per declaration kind:

- `ability`: `start`, `fixed` (or per-state `State.fixed`), `finish`, `cancel`
- `behavior`: `spawn`, `despawn`, `fixed` (or per-state)
- `trigger`: `on_enter`, `on_exit`
- `interaction`: `can_interact` (returns `bool`), `interact`

### Statements

`let`, assignment (including `+=`, `-=`, `*=`, `/=`), `if` / `else`, `while`,
`for i in a..b`, `return`, `enter State`, expression statements (calls).

### Expressions

Arithmetic, comparison, `&&` / `||` / `!` (short-circuit), field access,
indexing, record literals `Name { field: expr, ... }`, calls, and the literal
forms `tag"..."`, `cue"..."`, `prefab"..."`, `asset"..."`.

Builtins (compiled to opcodes or intrinsic host calls): `sin`, `cos`, `sqrt`,
`abs`, `min`, `max`, `clamp`, `lerp`, `distance`, `dot`, `cross`, `normalize`,
`length`, and the constants `pi`, `tau`.

### Grammar (EBNF)

```
module        = { import } { top_decl } ;
import        = "import" STRING ;
top_decl      = component | enum_decl | script_decl | function | const_decl ;

script_decl   = script_kind IDENT "{" { block_item } "}" ;
script_kind   = "ability" | "behavior" | "trigger" | "interaction" ;
block_item    = const_decl | param_decl | state_decl | function ;
state_decl    = "state" IDENT ;

component     = "component" IDENT "{" { field } "}" ;
field         = IDENT ":" type [ "=" literal ] ;
enum_decl     = "enum" IDENT "{" IDENT { "," IDENT } [ "," ] "}" ;
const_decl    = "const" IDENT ":" type "=" literal ;
param_decl    = "param" IDENT ":" type "=" literal ;

function      = "fn" [ IDENT "." ] IDENT "(" [ fparam { "," fparam } ] ")"
                [ "->" type ] block ;
fparam        = IDENT ":" type ;

type          = "bool" | "i32" | "i64" | "f32" | "f64" | "Vec2" | "Vec3"
              | "Quat" | "Entity" | "TagId" | IDENT
              | "[" type ";" INT "]" ;

block         = "{" { statement } "}" ;
statement     = let_stmt | assign | if_stmt | while_stmt | for_stmt
              | return_stmt | enter_stmt | expr_stmt ;
let_stmt      = "let" IDENT [ ":" type ] "=" expr ;
assign        = place ( "=" | "+=" | "-=" | "*=" | "/=" ) expr ;
place         = IDENT { "." IDENT | "[" expr "]" } ;
if_stmt       = "if" expr block [ "else" ( if_stmt | block ) ] ;
while_stmt    = "while" expr block ;
for_stmt      = "for" IDENT "in" expr ".." expr block ;
return_stmt   = "return" [ expr ] ;
enter_stmt    = "enter" IDENT ;
expr_stmt     = expr ;

expr          = or_expr ;
or_expr       = and_expr { "||" and_expr } ;
and_expr      = cmp_expr { "&&" cmp_expr } ;
cmp_expr      = add_expr [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) add_expr ] ;
add_expr      = mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr      = unary { ( "*" | "/" | "%" ) unary } ;
unary         = [ "!" | "-" ] postfix ;
postfix       = primary { "." IDENT | "[" expr "]" | "(" [ args ] ")" } ;
args          = expr { "," expr } ;
primary       = literal | IDENT | "(" expr ")" | record_lit
              | "tag" STRING | "cue" STRING | "prefab" STRING | "asset" STRING ;
record_lit    = IDENT "{" [ IDENT ":" expr { "," IDENT ":" expr } [ "," ] ] "}" ;
literal       = INT | FLOAT | "true" | "false" ;
```

Statements terminate at newline unless the line ends inside an open bracket or
after a binary operator or comma; `;` is an accepted separator.

## Examples

### Script-defined component plus hookshot ability

`scripts/abilities/hookshot.t`, referenced by
`abilities/player/hookshot.sability` (which declares the activation tag,
required/blocked tags, and may override the `param` values):

```
component HookshotState {
    target: Vec3
    target_entity: Entity
    pulling: bool = false
}

ability Hookshot {
    param range: f32 = 1800.0
    param pull_speed: f32 = 2600.0
    param stop_distance: f32 = 80.0

    state Pulling

    fn start(ctx: AbilityContext) {
        let owner = ctx.owner
        let hit = ctx.physics.raycast(ctx.aim_origin, ctx.aim_direction, range, QueryMask.Hookshot)

        if !hit.valid || !hit.entity.has_tag(tag"hookshot.anchor") {
            ctx.cancel()
            return
        }

        ctx.commands.add(owner, HookshotState { target: hit.position, target_entity: hit.entity })
        owner.add_tag(tag"state.hookshot.active")
        ctx.cue(cue"hookshot.fire")
        enter Pulling
    }

    fn Pulling.fixed(ctx: AbilityContext) {
        let owner = ctx.owner
        let target = owner.HookshotState.target

        ctx.movement.pull_toward(target, pull_speed, stop_distance)

        if distance(owner.Transform.position, target) <= stop_distance {
            owner.remove_tag(tag"state.hookshot.active")
            ctx.commands.remove(owner, HookshotState)
            ctx.finish()
        }
    }

    fn cancel(ctx: AbilityContext) {
        ctx.owner.remove_tag(tag"state.hookshot.active")
        ctx.commands.remove(ctx.owner, HookshotState)
    }
}
```

Semantics to note:

- `owner.HookshotState.target` is direct reflected field access. It compiles to
  a bind-table load/store over `World::GetComponentRaw`; if the component is
  absent it traps. `entity.has(HookshotState)` is the guard.
- Tag grant/revoke mutates the entity's `GameplayTagContainer` in place. That
  is a field write, not a structural change, so it is immediate and visible to
  `Changed<GameplayTagContainer>`.
- `ctx.commands.add` / `ctx.commands.remove` are structural and therefore
  deferred: they enqueue into the owning system's `CommandBuffer` and flush at
  that system's phase boundary. The record literal payload is captured by value
  (the raw path copies bytes), so `Pulling.fixed` sees the component on the
  next tick.
- `ctx.movement.pull_toward` writes a movement-intent component consumed by the
  native movement systems. `ctx.cue` appends to a bounded cue event buffer
  consumed by presentation systems at Update. The script orchestrates; engine
  systems move, animate, and play audio.
- `param` values may be overridden by the `.sability` asset (`Range: 1800`),
  so a designer tunes the hookshot without touching the script.

### Door interaction

```
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
```

### Pickup behavior (fixed tick, deterministic time from the tick index)

```
component BobMotion {
    base_height: f32
    amplitude: f32 = 12.0
    frequency: f32 = 0.8
}

behavior Pickup {
    fn spawn(ctx: BehaviorContext) {
        ctx.entity.BobMotion.base_height = ctx.entity.Transform.position.y
    }

    fn fixed(ctx: BehaviorContext) {
        let e = ctx.entity
        let t = f32(ctx.tick) * ctx.dt
        e.Transform.position.y = e.BobMotion.base_height
            + sin(t * e.BobMotion.frequency * tau) * e.BobMotion.amplitude
    }
}
```

### Trigger volume

```
trigger BurnField {
    fn on_enter(ctx: TriggerContext) {
        if !ctx.other.has_tag(tag"character.player") { return }
        ctx.other.add_tag(tag"damage.burning")
    }

    fn on_exit(ctx: TriggerContext) {
        ctx.other.remove_tag(tag"damage.burning")
    }
}
```

## Explicit non-goals (v1.0)

No classes, inheritance, virtual methods, constructors, or destructors. No
interfaces. No closures or function values. No coroutines or `wait` (suspended
stacks are persistent VM state; principle 15). No generics. No dynamic arrays
or maps. No script-side heap or visible allocation. No runtime strings or
string manipulation. No raw ECS queries. No registry mutation. No scheduler
integration. No threads or user-visible concurrency. No OS file or network IO.
No wall-clock time. No unseeded randomness. No pointer arithmetic or address
identity. No `eval` or runtime code loading. No script-defined ECS systems. No
direct physics mutation, renderer access, or audio playout. No blocking asset
loads.

## Register bytecode and VM

### Instruction set shape

Fixed-width 32-bit instructions, register-based, on the order of 48 opcodes:

- moves and constant loads (constant-pool indexed),
- `i32`/`i64` and `f32`/`f64` arithmetic and conversions,
- vector ops as opcodes for the hot path (`vadd`, `vsub`, `vscale`, `vdot`,
  `vlength`); the rest of the math surface is intrinsic builtins,
- comparisons and conditional/unconditional branches,
- `component_load` / `component_store` (bind-table index; see linking),
- tag ops (`tag_has`, `tag_add`, `tag_remove` against the bind table),
- `host_call` (import-table index, arguments in a contiguous register window),
- record init (component literal construction into a scratch frame),
- `enter_state`, `return`,
- a budget tick fused into back-branches and calls.

Registers are untagged 64-bit slots. Type safety is established statically by
the compiler and re-checked at load by `ScriptBytecodeValidator`: jump targets,
register bounds, type flow per register, constant and import indices, and the
presence of budget instrumentation. Call depth is capped (32); each function
declares its register frame size; there is no heap.

Why register bytecode: fewer dispatches than a stack machine, direct mapping
from a simple SSA-ish IR, natural static validation (register types are
checkable per instruction), readable disassembly, and a clean host-call
boundary (argument windows instead of stack juggling).

### Linking: nothing runtime-fragile bakes into bytecode

Two facts force a link step: gameplay tag ids are registration-order runtime
values (never stable across builds), and native component layouts can change
without recompiling scripts. So bytecode references symbolic constants
("hookshot.anchor"; ("Transform", "local.position.y")) and module load links
them into runtime bind tables:

- tag name to `GameplayTagId` via `GameplayTagRegistry`,
- component name plus dotted field path to
  `(ComponentId, offset, size, FieldScalar)` via
  `IComponentSerializer::RuntimeFields()` (the same reflection the editor
  inspector uses); consecutive scalar leaves are grouped so a `Vec3` access is
  one bind entry,
- asset refs to `AssetId` / handles via the registry,
- host function signature (name plus type signature plus API version) to a
  `ScriptHostApiTable` slot.

Link failure is a load error naming the missing symbol, never a runtime
surprise.

### Budgets

Per-callback instruction budget (cvar `script.budget`, default on the order of
100k), decremented on back-branches and calls; exhaustion traps. VM execution
is serial inside the invoking system. This is deliberate: scripts are entity
glue and sit far under the roughly 1ms chunk-parallel profile gate. Parallel VM
execution is deferred and would require the same serial-reference-identical
treatment as every other parallel path.

## Host API boundary

`ScriptHostApiTable` is a versioned, declared table of typed host functions.
It is the closed world: no FFI, no dlopen, nothing reachable that is not in the
table. Groups for v1.0:

- **Context values** (read-only): `ctx.tick: i64` (`FixedSimTime::TickIndex`),
  `ctx.dt: f32` (fixed), `ctx.owner` / `ctx.entity` / `ctx.target` /
  `ctx.other: Entity` (per context kind), `ctx.aim_origin` /
  `ctx.aim_direction: Vec3` (ability contexts).
- **Entity**: `has(Component) -> bool`, reflected field load/store,
  `has_tag(t) -> bool` (exact), `has_tag_under(t) -> bool` (inclusive
  descendant, matching the engine's query-side `IsDescendantOf` convention),
  `add_tag(t)`, `remove_tag(t)`, `alive() -> bool`.
- **Commands** (structural, deferred through the owning system's
  `CommandBuffer`): `add(entity, ComponentLiteral)`, `remove(entity,
  Component)`, `destroy(entity)`, `spawn(prefab, at: Vec3)` (fire-and-forget in
  v1.0; see prerequisite 3).
- **Physics queries** (read-only, over `PhysicsQueries`):
  `raycast(origin, dir, max_distance, mask) -> RayHit { valid, entity,
  position, normal, distance }`, `sweep_sphere(...) -> SweepHit`,
  `overlap_sphere(center, radius, mask) -> OverlapHits { count, entities:
  [Entity; 16] }` (bounded, no allocation).
- **Movement requests** (intent components consumed by native movement
  systems): `pull_toward(target, speed, stop_distance)`, `impulse(v)`,
  `halt()`.
- **Cues**: `cue(cueref)`, `cue_at(cueref, position)`; appends to a bounded
  per-frame cue event buffer consumed by presentation systems at Update.
- **Random**: `ctx.random.f32() -> f32` in [0,1), `ctx.random.range(lo, hi) ->
  i32`, `ctx.random.unit_vec3() -> Vec3`; deterministic seeded streams (see
  Determinism).
- **Ability control**: `ctx.cancel()`, `ctx.finish()`.
- **Log**: `log("...", args...)` with a compile-time format string; routed to a
  per-script log channel.

Every mutating call flows as data into existing systems (directive 3): intents,
tags, cue events, command buffers. The one asymmetry, in-place tag mutation, is
allowed because `GameplayTagContainer` grant/revoke is a component field write,
not a structural change.

### Prerequisite engine work

Each item is small and independently testable; none is speculative (each is
required by the v1.0 milestone that consumes it):

1. `World::RegisterComponentRaw(name, size, align, isTag, fields)`: a
   non-template registration entry point (the storage and lookup paths are
   already type-erased at the `ComponentTypeId` / `ComponentId` level), plus a
   non-template `ScriptComponentSerializer : IComponentSerializer` synthesizing
   `RuntimeFields()` and `DefaultBytes()` from the T component declaration.
2. Type-erased `CommandBuffer::AddComponentRaw` / `RemoveComponentRaw`
   overloads (the World-side raw flush paths already exist).
3. A spawn-request mechanism that yields an `EntityId` at a phase boundary
   (`CommandBuffer::CreateEntity()` cannot return the id today). v1.0 can ship
   with fire-and-forget spawning if this slips.
4. `core/random`: seeded deterministic RNG streams (PCG32-class), keyed by
   (world seed, stream id, TickIndex). Does not exist today and is useful
   engine-wide, not just for scripts.
5. Native `TriggerVolumeSystem` and `InteractionSystem` (new, built over
   `PhysicsQueries::OverlapShape` and input intents). The script bridges attach
   to them; they are useful without scripts and are specified independently.
6. `AbilityDefinition` gains an optional script asset reference (the
   `.sability` `Script:` field), plus `param` override data.
7. Physics query masks (the `QueryMask` argument): `PhysicsQueries::Raycast`
   currently takes no filter. Until masks land, the raycast binding filters by
   tag on the hit entity.

## Runtime architecture

New engine subsystem `engine/include/script/` + `engine/src/script/` (picked up
by the existing glob; no CMake change). Pieces:

```
ScriptCompiler          cook/dev only, gated SENCHA_ENABLE_COOK
ScriptImporter          IAssetImporter for .t -> .tbc, cook only
ScriptModule            loaded .tbc: bytecode, pools, bind requests
ScriptBytecodeValidator load-time structural and type-flow checks
ScriptAssetLoader       IAssetLoader (CPU-only; AudioClipAssetLoader pattern)
ScriptCache             AssetCache<ScriptCache, ScriptHandle, ScriptEntry>
ScriptVm                the interpreter; owned by the invoking systems' shared runtime
ScriptHostApiTable      the declared, versioned host surface
ScriptDebugSymbols      line table + disassembly support
ScriptBehaviorSystem    FixedLogic; invokes behavior callbacks
AbilityScriptBridge     attaches ability callbacks to AbilityKit activation
TriggerScriptBridge     attaches to TriggerVolumeSystem
InteractionScriptBridge attaches to InteractionSystem
```

Native systems own execution: `AbilitySystem` invokes ability callbacks,
`TriggerVolumeSystem` trigger callbacks, `InteractionSystem` interaction
callbacks, `ScriptBehaviorSystem` entity behavior callbacks, all from their
`FixedLogic(FixedLogicContext&)` in the `FramePhase::Simulate` fixed-tick lane.
Callbacks are system details: never standalone scheduled systems, never a third
concurrency lane. Callback order within a system follows its cached query's
chunk order, serial, which is deterministic given identical structural history.
Structural changes enqueue into the owning system's `CommandBuffer` and flush
at its phase boundary. Any cross-frame script work (hot-reload recompilation)
runs on `AsyncTaskQueue` and commits at `FramePhase::DrainAsyncTasks`.

Per-entity attachment is data: an engine component (for example
`ScriptBehavior { script: AssetRef, state: <name-interned id> }`) carries the
script handle and current state name. All script-visible persistent state lives
in components; the VM holds nothing between callbacks (principle 15).

The VM does not include gameplay headers upward; the bridges depend on the VM,
never the reverse. A fitness script guards the direction (see Fitness tests).

## Determinism model

Allowed, by construction:

- the fixed tick index (`ctx.tick`, from `FixedSimTime::TickIndex`),
- fixed `ctx.dt`,
- deterministic seeded random streams from context,
- deterministic math (single-threaded interpretation, fixed evaluation order,
  explicit conversions; float arithmetic is f64 in registers with one defined
  rounding on `f32` stores),
- stable entity handles (generational) and stable asset handles,
- bounded event and callback order,
- instruction budgets,
- load-time bytecode validation.

Absent, by construction (unreachable rather than policed): wall-clock time, OS
threads, file IO, network IO, pointer arithmetic, address identity, unseeded
randomness, dynamic eval, direct registry access, direct job scheduling,
blocking asset loads.

Enforcement is three layers: the closed host table (the only way out of the
VM), the bytecode validator (the only way into it), and trap semantics (every
failure mode is deterministic and terminal for the callback, in the same place
every run).

The determinism gate extends the existing differential pattern
(`test/runtime/ZoneParallelTests.cpp`): run a scripted scenario for N fixed
ticks twice in one process, and again serial (`JobWorkerCount = 0`) versus
parallel engine configuration, comparing per-tick component-state hashes. The
cvar-gated script checksum (instruction counts plus component-write hash) makes
divergence point at the offending callback.

## Asset, cook, and hot-reload model

Source: `scripts/**/*.t`. Cooked: `.tbc` under `.cooked/`, registered in
`CookedCacheIndex` with the source content hash; the import closure is hashed
so a library edit invalidates dependents. Bump `kCookedCacheIndexVersion` on
bytecode format changes to force a recook. `AssetType::Script = 7` is already
reserved; `.tbc` maps to it in `AssetTypeFromExtension`.

`.tbc` container:

- header: magic, bytecode format version, host API version, determinism flags,
- bytecode, constant pool, type table,
- declared component schemas (names, fields, defaults),
- symbolic bind requests: components/fields, tags, cues, assets, host imports
  (name plus signature),
- source hash and bytecode hash,
- debug symbols and source line table (a strippable section for shipping),
- required engine API surface (minimum host API version per import).

`ScriptImporter : IAssetImporter` (extension `.t`, `SENCHA_ENABLE_COOK` only)
runs `ScriptCompiler` and writes the artifact: pure, bytes in, artifacts out,
errors returned not logged, per the importer contract.

`ScriptAssetLoader : IAssetLoader`, modeled on `AudioClipAssetLoader` (the
CPU-only loader): `LoadStaged` parses and validates the container off-thread
(pure, no engine state); `CommitTyped` links bind tables against the live
registries and schemas on the owner thread, registers the script's components,
and returns a `ScriptHandle` from `ScriptCache`. Registration follows the
documented "adding a new asset type" exercise in `docs/assets/architecture.md`
(cache member in `RuntimeAssets`, loader owned by `AssetSystem`,
`LoaderFor(type)` case, extension mapping).

Hot reload: `AssetSourceWatcher` learns the `.t` extension;
`AssetHotReloader::ReloadSource` re-runs the importer; the loader's
`CommitReload` swaps the module in the existing cache slot via `ReloadInPlace`
(slot index, generation, and refcount preserved: every live handle sees the new
module with no invalidation protocol, exactly the material/audio pattern).
After the swap: relink bind tables, migrate component instances (answer 17),
re-enter active state machines by name, cancel instances whose state vanished.
A failed compile logs and leaves the live module untouched (best-effort, same
policy as every other asset type). The compiler ships in dev builds only
(`SENCHA_ENABLE_COOK`, the glslang pattern); shipping builds load `.tbc` only.

## Debugging and error reporting

- **Compile time (cook / hot reload):** errors carry file:line:col, a source
  excerpt with a caret, and expected-versus-found types. Error-message quality
  is a first-class fitness criterion with golden-file tests, not a polish item.
- **Load / link time:** errors name the missing or mismatched tag, component,
  field, asset, or host import, and the script that requested it.
- **Runtime:** the trap model (answer 18): deterministic cancel of the
  callback, one structured log with script and line, editor console surfacing,
  entity error badge in PIE.
- **Tooling (v1.0):** `script.disasm <path>` console command (disassembly with
  source-line annotations), `script.budget` and `script.checksum` cvars, a
  per-script log channel. Interactive stepping is v2.0; the line table and
  disassembly are designed so a stepper can be added without a format change.

## Fitness tests

gtest suites under `test/script/` (new stanza in `test/CMakeLists.txt`
mirroring `jobs_tests`):

- compiler golden tests: source to expected disassembly,
- error golden tests: bad source to expected message shape,
- validator rejection tests: hand-corrupted bytecode must be refused,
- VM semantics tests: hand-assembled modules, no compiler dependency,
- link/bind tests: missing tag/component/field/host import fails load with the
  right diagnostic,
- trap tests: each trap kind fires deterministically at the same instruction,
- budget tests: exhaustion traps; budget cvar respected,
- hot-reload migration tests: field add/remove/retype, state rename, state
  removal,
- determinism differential tests: scripted scenario, N ticks, run-vs-run and
  serial-vs-parallel engine config, per-tick component hash equality,
- schedule integration tests following `test/core/EngineScheduleTests.cpp`.

Architecture fitness scripts (ctest shell checks alongside
`scripts/check_module_abi.sh`):

- `check_script_layering.sh`: `engine/src/script/` includes nothing upward, no
  gameplay headers into the VM, compiler translation units only under the cook
  gate,
- `check_script_host_surface.sh`: the host table is the only externally
  reachable surface; grep-guards against wall-clock, file, socket, and thread
  symbols anywhere under `script/`.

End-to-end fitness: the hookshot workflow as an integration test (grant tags,
queue an `AbilityActivation`, tick, assert the component/tag/cue trajectory
against the script), plus the scripted determinism-gate scenario in the
fixed-tick differential suite.

## Risk comparison

| | Integration cost | Determinism | Static safety | Reflection/ECS fit | Designer fit | Long-term risk |
|---|---|---|---|---|---|---|
| Lua | Low | Poor natively; fork-and-audit (GC timing, table iteration order, stdlib) | None (dynamic) | Hand-written mirror-schema binding layer | Good | Fork maintenance forever; no cook-time validation |
| AngelScript | Medium | Unaudited; GC plus registered behaviors | Good (static) | Class/OO model fights component records | Medium | Large surface, OO pull, embedding quirks |
| Wren | Low | Fibers plus GC, unaudited | None (dynamic) | Class-based, poor record fit | Medium | Lua risks minus Lua's ecosystem |
| Squirrel | Low | Unaudited, dynamic | None | OO-flavored, poor fit | Medium | Aging project |
| Custom T | High upfront (compiler, VM, tooling) | By construction | Full, at cook time | Native: the same TypeSchema path | Designed for it | Language maintenance is ours forever; mitigated by a frozen-tiny v1.0 |

The honest cost statement: T's long tail is tooling (editor highlighting,
error-message polish, documentation) and being a single-team language. The
mitigation is scope discipline: v1.0 is small enough to specify in this one
document, and every off-the-shelf option would still require most of the
binding, cook, hot-reload, and determinism work while surrendering static
validation and the unified reflection path. The engine work (loaders, bridges,
raw registration, RNG, trigger/interaction systems) is common to every option;
only the compiler and interpreter are T-specific cost.

## Roadmap placement

Phase 2.5 in `real-engine-roadmap.md`, between gameplay runtime foundations and
the content/shipping pipeline. It sequences after Phase 2 because ability and
behavior scripts consume Phase 2's physics queries, movement, and input
mapping. The roadmap entry restates the hard constraints and links here.

## Prototype order

Examples-first, then VM-first; the compiler comes after the VM and before
integration.

1. **Examples plus host API sketch** (documents, days): the five example
   scripts above plus the complete host table signature list, reviewed against
   real gameplay needs from the two target games. The cheapest place to change
   the language is before any code exists.
2. **Bytecode plus VM core** (first code milestone): instruction set,
   `ScriptModule` container, validator, interpreter, trap and budget
   semantics, hand-assembled-module gtests, the determinism differential
   harness. The bytecode is the contract everything else targets.
   Gate: hand-assembled modules run, validate, trap, and hash identically
   run-over-run.
3. **Compiler front end**: lexer, parser, typechecker, codegen against golden
   disassembly tests; error-message golden tests from day one.
   Gate: the five example scripts compile to validated bytecode.
4. **Asset, cook, and hot-reload integration**: importer, loader, cache,
   watcher extension, migration.
   Gate: edit hookshot.t, PIE picks it up without restart, state migrates.
5. **Bridges plus prerequisite engine work**: raw component registration,
   type-erased CommandBuffer ops, `core/random`, the ability bridge first
   (AbilityKit exists today), then `ScriptBehaviorSystem`, then the new
   trigger/interaction systems and their bridges.
6. **Hookshot end to end, determinism gate, editor surfacing.**
   Gate: the Phase 2.5 roadmap gate.

Rationale: examples fix the surface while changing it is free; building the VM
before the compiler gives every compiler test a validated execution target and
keeps the compiler honest against a frozen instruction set; integration last
means the asset plumbing carries a format that has already stopped moving.

## Deferred to v2.0

Pattern matching (`match` over enums). Dynamic arrays and maps. Closures.
Coroutines / `wait` (only if a design preserves principle 15, for example
compiling waits into generated states). Script-defined ECS systems and raw
queries. Arbitrary gameplay event subscription beyond the fixed callback set.
Generics. Interactive debugger stepping (the line table and disassembly ship in
v1.0). Spawn-with-id ergonomics if prerequisite 3 slips. Any networking or
replication awareness. Parallel VM execution (behind the profile gate, with the
serial reference path identical).
