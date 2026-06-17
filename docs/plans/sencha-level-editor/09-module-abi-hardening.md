# Hardening the Game-Module ABI Seam

Status: plan (not yet implemented).

## Context

Sencha is an engine, not a monorepo app: **game modules are built and shipped
independently** of the engine binary (`game.so`/`game.dll`, loaded via
`GameModuleLoader`). That makes the native module boundary a first-class,
long-lived contract — the place where an engine update can silently break a
module someone else built months earlier.

Today the boundary is:

- One `extern "C"` entry point, `SenchaCreateGameModule()`, returning an
  `IGameModule*`.
- A handshake: `IGameModule::AbiVersion()` (baked to `SENCHA_GAME_ABI_VERSION`
  at module build) compared against the host's value; mismatch is refused with a
  message *before* `Register()` is called.
- **Everything else crosses as C++ vtables created inside the module** — most
  importantly `ComponentSerializer<T>` (deriving from the engine's
  `IComponentSerializer`), plus rich references in `GameModuleContext`
  (`ComponentSerializerRegistry&`, `ConsoleRegistry&`).

### The incident this addresses

Adding a virtual method to `IComponentSerializer` (an `EditorVisual` hint) and
**not** bumping `SENCHA_GAME_ABI_VERSION` shipped a host whose vtable layout for
that interface differed from an already-built module's. The handshake saw
`2 == 2` and loaded it; the first cross-boundary virtual call
(`ComponentSerializer<GrappleHook>::HasComponent`) dispatched to the wrong slot
and segfaulted inside the module. The crash was far from the cause and had no
diagnostic.

Two root causes:

1. **The version is hand-bumped and coarse.** It nominally covers "the
   registration surface," but that surface is wide (every type the module
   derives from or whose layout it sees), and a single integer kept in sync by
   memory is exactly the thing that failed.
2. **The check rides the very vtable it validates.** `module->AbiVersion()` is a
   virtual call; if `IGameModule`'s own layout ever skews, validating the ABI is
   itself undefined behaviour.

## Goals

1. ABI skew is detected at load and reported clearly — **never a crash** (the
   "make the stack transparent about what's failing" requirement).
2. The compatibility token reflects the **actual** ABI surface, not a human
   remembering to bump a number.
3. Reduce the surface so routine engine changes (like adding a serializer
   method) **can't** break a shipped module.

## Non-goals

- A language-agnostic / COM-style fully stable ABI.
- Rewriting the component or console systems.
- Supporting modules built by a *different* compiler family in the near term
  (we will detect and refuse it cleanly rather than support it).

## The surface today (threat model)

What crosses the boundary, by fragility:

- **Stable:** the `extern "C"` factory name and signature.
- **Guarded (coarsely):** `IGameModule`, `GameModuleContext`, `EngineHostInfo`
  shape — covered by the manual version, when remembered.
- **Unguarded in practice:**
  - `IComponentSerializer`'s vtable — modules instantiate `ComponentSerializer<T>`,
    so the module binary hard-codes this layout. (The incident.)
  - `ComponentSerializerRegistry` / `ConsoleRegistry` member layout — modules
    hold references and call in, seeing inline/layout decisions.
  - Each serialized component's struct layout (via `TypeSchema`/`ComponentSerializer<T>`).
  - Toolchain skew: compiler, C++ stdlib (libstdc++ vs libc++), build type,
    sanitizers, pointer size — none currently checked.

## Plan

Three tiers, increasing cost. Tier 0–1 are the proportionate response and would
have caught the incident; Tier 2 is the structural fix that makes independent
module shipping genuinely safe.

### Tier 0 — Make the existing handshake trustworthy (cheap; do first)

1. **Validate via C linkage, not a virtual.** Add an `extern "C"` token to the
   module's exported surface (e.g. `SenchaGameAbiToken()` returning a POD, or the
   factory returns `{ token, IGameModule* }`). The loader checks the token
   *before* touching any C++ vtable, so validating the ABI never depends on the
   ABI being valid.
2. **Expand the token to a build-identity record**, compared field-by-field with
   precise messages:
   - ABI version (as today),
   - compiler id + major version, C++ stdlib id + version,
   - build type (debug/release), pointer size, sanitizer flags.
   This catches the "compiled differently" class a version integer never will
   ("module built with libc++; host is libstdc++ — refusing").
3. **Layout asserts for POD types that cross** (`GameModuleContext`,
   `EngineHostInfo`, any new descriptor structs): record `sizeof`/`alignof` on
   both sides and compare. Cheap, catches struct drift.

### Tier 1 — Make the version automatic, not remembered (kills the incident's cause)

The version failed because bumping it is manual. Move it to the build:

- **Derive an ABI fingerprint at build time** from the actual module-ABI header
  set — a hash of those headers (or their preprocessed form) embedded in both
  engine and module via a generated header. The loader compares fingerprints;
  any change to an ABI header yields a different fingerprint and a clean
  rejection, with no one needing to remember anything.
- Over-sensitivity (a non-ABI header edit changes the hash) is acceptable and
  arguably right for an engine: a false positive forces a harmless rebuild; a
  false negative is the segfault we just had. Bias toward refusing.
- Pragmatic fallback if a header hash is too blunt: keep the integer but add a
  **golden-layout test** (a checked-in table of `sizeof`/`offsetof` for the
  surface types) that fails when layout changes without a deliberate update +
  bump. Turns "forgot to bump" into a red test instead of a runtime crash.

### Tier 2 — Narrow the surface (the real tightening)

The deepest fragility is that modules **instantiate `ComponentSerializer<T>`**,
baking `IComponentSerializer`'s vtable into the module. Remove that:

- **Modules register components with POD descriptors + C-ABI function pointers**
  (per type: save / load / has / remove / default-bytes / runtime-fields), and
  the **engine** constructs the serializer objects on its side. Then
  `IComponentSerializer`'s vtable lives entirely engine-side and changing it can
  never break a module — the exact incident becomes impossible.
- **Replace rich references in `GameModuleContext`** (`...Registry&`) with a
  small, versioned **C-ABI registration interface** (a struct of function
  pointers the engine supplies). Modules call functions; they never see registry
  member layout.
- **Tool-only concerns travel as data, not vtable.** An editor hint like
  `EditorVisual` becomes a field in the registration descriptor, not a virtual on
  a module-derived interface — so editor/tooling evolution cannot churn module
  ABI. (Directly relevant: the incident was an editor feature breaking the
  module boundary.)
- **End state:** a module's required ABI is (a) the `extern "C"` factory and
  token, and (b) one versioned C-ABI registration interface (function pointers +
  POD descriptors). Rich C++ types stay engine-internal.

### Tier 3 — Enforcement / CI

- A loader test with a deliberately-mismatched module fixture asserting it is
  **rejected with a message**, not crashed — a regression guard for the
  handshake itself.
- CI builds the sample/test module against the engine and load-tests it; ideally
  also builds it with a deliberately-skewed flag to prove rejection.
- A short "what is in the module ABI surface" doc + the standing guideline:
  **tool-only concerns never ride module-ABI interfaces.**

## Recommendation / phasing

- **Tier 0 + Tier 1 now.** Small, and together they convert silent vtable
  corruption into a clear load-time error and make skew detection automatic —
  this is the stated minimum ("make the stack transparent") and would have caught
  the incident outright.
- **Tier 2 when ready to invest.** It is the structural change that lets the
  engine evolve `IComponentSerializer` (and friends) without breaking modules
  already shipped — the property an independently-distributed-module engine
  actually needs. It is a real refactor of the registration path and should be
  scoped on its own.
- **Tier 3 alongside**, so the guarantees are regression-tested.

## Risks / notes

- The factory/token change in Tier 0 is itself an ABI change → one final manual
  `SENCHA_GAME_ABI_VERSION` bump, after which the automated fingerprint carries
  the load.
- Tier 2 descriptors must cover everything `ComponentSerializer<T>` does today
  (serialization, storage registration, runtime fields, defaults); missing one
  reintroduces a C++ type across the seam.
- Cross-compiler module support is explicitly out of scope: we detect and refuse,
  we don't reconcile. Revisit only if third-party modules must use a different
  toolchain.
- This mirrors the project's existing ethos — mechanical guardrails over human
  discipline (cf. the `meshedit_dependency_directions` ctest). The fingerprint is
  the same idea applied to the module boundary.
```
