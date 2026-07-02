# T v1.0 implementation spec (normative)

This document is normative for the v1.0 implementation of T. Where it and the
design doc (`t-language.md`) disagree, this document wins. Where this document
is silent, the design doc governs. The intent: an implementing session
transcribes decisions from here; it does not design.

Contents:

- A. Instruction set (bytecode v0, frozen)
- B. `.tbc` container layout
- C. Host API table
- D. Semantics appendix
- E. Milestone checklists

## A. Instruction set (bytecode v0, frozen)

### Machine model

- Register machine. Each function declares a frame of up to 256 slots.
- A slot is untagged 64 bits. Types are static: the compiler assigns a type to
  every slot at every program point, and the validator recomputes and checks
  that assignment. The interpreter trusts validated bytecode and carries no
  runtime tags.
- Slot types in the validator lattice: `i32`, `i64`, `f64`, `bool` (values 0
  or 1), `entity` (EntityId: index in the low 32 bits, generation in the high
  32), `tag` (GameplayTagId in the low 32 bits), and aggregate views.
- Aggregates occupy consecutive slots: `Vec2` = 2 x f64, `Vec3` = 3 x f64,
  `Quat` = 4 x f64, host record types per their declared layout (section C).
  `f32` values are held in f64 slots; `f32` semantics are produced by the
  explicit `RNDF32` rounding instruction and by component stores.
- Call depth cap: 32 frames. Exceeding it traps (`T_TRAP_CALL_DEPTH`).
- Execution budget: a per-callback counter (cvar `script.budget`) decremented
  once per executed instruction; reaching zero traps (`T_TRAP_BUDGET`). This
  replaces the design doc's earlier "fused into back-branches and calls"
  wording: one decrement per instruction is simpler and equally deterministic.

### Encoding

Instructions are one or two little-endian 32-bit words.

```
word 0:  bits 0-7   opcode
         bits 8-15  A
         bits 16-23 B
         bits 24-31 C
```

Field interpretations per format:

- `ABC`: A, B, C are 8-bit register indices or small immediates.
- `ABx`: A is 8-bit; Bx is the unsigned 16-bit value in bits 16-31.
- `AsBx`: A is 8-bit; sBx is the signed 16-bit value in bits 16-31.
- `sAx`: bits 8-31 as a signed 24-bit value.
- `EXT`: the instruction has a second word; its layout is given per opcode.

Branch offsets are in words, relative to the instruction after the branch
(and after its extension word, if any).

### Opcode table

Traps use the codes defined in section D.7. "wraps" means two's complement.

| Hex | Mnemonic | Fmt | Operands | Semantics | Traps |
|-----|----------|-----|----------|-----------|-------|
| 0x00 | NOP | ABC | none | no effect | |
| 0x01 | MOV | ABC | A=dst, B=src | copy one slot | |
| 0x02 | MOVW | ABC | A=dst, B=src, C=count | copy C consecutive slots (C in 1..255) | |
| 0x03 | LDC | ABx | A=dst, Bx=const index | load 64-bit constant-pool entry | |
| 0x04 | LDI | AsBx | A=dst, sBx=imm | load sBx as i32 | |
| 0x05 | LDP | ABx | A=dst, Bx=param index | load resolved `param` value (one slot; aggregates use consecutive param indices) | |
| 0x10 | ADDI | ABC | A=dst, B, C | i32 add, wraps | |
| 0x11 | SUBI | ABC | | i32 sub, wraps | |
| 0x12 | MULI | ABC | | i32 mul, wraps | |
| 0x13 | DIVI | ABC | | i32 truncated divide; INT_MIN/-1 yields INT_MIN | C==0: T_TRAP_DIV_ZERO |
| 0x14 | MODI | ABC | | i32 remainder, sign follows dividend (C++ semantics); INT_MIN%-1 yields 0 | C==0: T_TRAP_DIV_ZERO |
| 0x15 | NEGI | ABC | A=dst, B=src | i32 negate, wraps at INT_MIN | |
| 0x16 | ADDL | ABC | | i64 add, wraps | |
| 0x17 | SUBL | ABC | | i64 sub, wraps | |
| 0x18 | ADDF | ABC | | f64 add, IEEE 754, round-to-nearest-even | |
| 0x19 | SUBF | ABC | | f64 sub | |
| 0x1A | MULF | ABC | | f64 mul | |
| 0x1B | DIVF | ABC | | f64 divide, IEEE (divide by zero yields +-inf, 0/0 yields NaN; no trap) | |
| 0x1C | NEGF | ABC | A=dst, B=src | f64 negate | |
| 0x20 | I2F | ABC | A=dst, B=src | i32 to f64 (exact) | |
| 0x21 | L2F | ABC | | i64 to f64 (round-to-nearest-even) | |
| 0x22 | F2I | ABC | | f64 to i32, truncate toward zero | NaN or out of i32 range: T_TRAP_CONV |
| 0x23 | I2L | ABC | | i32 sign-extend to i64 | |
| 0x24 | L2I | ABC | | i64 to i32, wraps (low 32 bits) | |
| 0x25 | RNDF32 | ABC | A=dst, B=src | round f64 to nearest f32 value (round-to-nearest-even), result stays f64 | |
| 0x30 | CEQI | ABC | A=dst bool, B, C | i32 equal | |
| 0x31 | CLTI | ABC | | i32 less-than | |
| 0x32 | CLEI | ABC | | i32 less-or-equal | |
| 0x33 | CEQL | ABC | | i64 equal | |
| 0x34 | CLTL | ABC | | i64 less-than | |
| 0x35 | CLEL | ABC | | i64 less-or-equal | |
| 0x36 | CEQF | ABC | | f64 equal (NaN compares false) | |
| 0x37 | CLTF | ABC | | f64 less-than (NaN operand: false) | |
| 0x38 | CLEF | ABC | | f64 less-or-equal (NaN operand: false) | |
| 0x39 | CEQE | ABC | | entity equal (index and generation) | |
| 0x3A | CEQT | ABC | | tag id equal (exact, no hierarchy) | |
| 0x3B | NOT | ABC | A=dst, B=src bool | logical not | |
| 0x40 | JMP | sAx | sAx=offset | unconditional branch | |
| 0x41 | BT | AsBx | A=cond bool, sBx=offset | branch if true | |
| 0x42 | BF | AsBx | A=cond bool, sBx=offset | branch if false | |
| 0x50 | VADD3 | ABC | A=dst base, B, C bases | Vec3 add, componentwise, x then y then z | |
| 0x51 | VSUB3 | ABC | | Vec3 sub | |
| 0x52 | VSCALE3 | ABC | A=dst base, B=src base, C=scalar reg | Vec3 scale | |
| 0x53 | VDOT3 | ABC | A=dst scalar, B, C bases | dot product, evaluated as ((x*x' + y*y') + z*z') | |
| 0x54 | VLEN3 | ABC | A=dst scalar, B=base | sqrt(dot(v,v)), IEEE sqrt | |
| 0x60 | CLD | ABC | A=dst base, B=entity reg, C=field-bind index | load field group into consecutive slots | dead entity: T_TRAP_ENTITY; component absent: T_TRAP_NO_COMPONENT |
| 0x61 | CST | ABC | A=entity reg, B=src base, C=field-bind index | store field group (f32 fields: RNDF32 rounding on store) | same as CLD |
| 0x62 | CHAS | ABC | A=dst bool, B=entity reg, C=component-bind index | entity has component | dead entity: T_TRAP_ENTITY |
| 0x63 | CLDX | EXT | A=dst base, B=entity reg; word2: bits 0-15 field-bind index, bits 16-23 index reg | array-field load with runtime index | index out of bounds: T_TRAP_BOUNDS; plus CLD traps |
| 0x64 | CSTX | EXT | A=entity reg, B=src base; word2 as CLDX | array-field store with runtime index | same |
| 0x70 | THAS | ABC | A=dst bool, B=entity reg, C=tag-bind index | exact tag present | dead entity: T_TRAP_ENTITY; no GameplayTagContainer: false, no trap |
| 0x71 | THASU | ABC | | tag present under ancestor (inclusive descendant, registry hierarchy) | same |
| 0x72 | TADD | ABC | A=entity reg, B=tag-bind index | grant one stack | dead entity: T_TRAP_ENTITY; no container or container full: T_TRAP_TAG_CAPACITY |
| 0x73 | TREM | ABC | A=entity reg, B=tag-bind index | revoke one stack (absent tag: no effect) | dead entity: T_TRAP_ENTITY |
| 0x80 | HCALL | ABC | A=arg window base, B=host-import index, C=arg slot count | call host function; return value written at window base | per import (section C) |
| 0x81 | AGETL | EXT | A=dst base, B=array base reg; word2: bits 0-15 element count, bits 16-23 index reg, bits 24-31 element slot size | local fixed-array load | index out of bounds: T_TRAP_BOUNDS |
| 0x82 | ASETL | EXT | A=array base reg, B=src base; word2 as AGETL | local fixed-array store | same |
| 0x83 | CALL | ABx | A=arg window base, Bx=function index | call module function; args at window, return value written at window base | depth: T_TRAP_CALL_DEPTH |
| 0x90 | ENTER | sAx | sAx=state index | set the pending state transition (last one wins) | unknown state index rejected by validator |
| 0x91 | RET | ABC | none | return, no value | |
| 0x92 | RETV | ABC | A=value base, B=slot count | return value | |

61 opcodes. Unassigned opcode values are validation errors, reserved for
bytecode v1.

Constraints baked into the encoding (validation errors, with the remedy in the
compile error message): at most 256 field-bind entries reachable by CLD/CST
(CLDX/CSTX use 16-bit indices), 256 component-bind entries, 256 tag-bind
entries, 256 host imports, 65536 constants, 65536 params, 65536 module
functions. A script that exceeds a bind cap gets a cook error telling the
author to split the script.

### Validator rules (ScriptBytecodeValidator)

Structural pass, then type pass. Both run at every module load; the compiler
is not trusted.

Structural:
1. Every instruction decodes to an assigned opcode; EXT opcodes have their
   second word inside the function's code range.
2. Every branch target is an instruction start inside the same function.
3. Every constant, param, bind, import, function, and state index is in range
   for its table.
4. Every function ends with RET or RETV on all paths (no fallthrough off the
   end).
5. Register indices (including aggregate spans: base + width - 1) fit the
   declared frame size.
6. ENTER appears only in functions attached to `ability` or `behavior`
   declarations.

Type pass (forward dataflow over the CFG, join = exact-match-or-error; the
compiler must insert explicit conversions so no join ambiguity exists):
7. Every operand slot has the type the opcode requires at every use.
8. HCALL argument windows match the import's declared signature (section C);
   CALL windows match the callee's signature.
9. CLD/CST/CLDX/CSTX slot groups match the bind entry's declared field group
   (scalar kinds and count).
10. RETV width matches the function's declared return type.

Any violation rejects the module with an error naming the function, code
offset, and rule.

## B. `.tbc` container layout

All integers little-endian. All section payloads 8-byte aligned, zero padded.

### Header (32 bytes)

```
offset size  field
0      4     magic: ASCII "TBC0" (54 42 43 30)
4      2     container version, u16 = 1
6      2     bytecode format version, u16 = 0
8      2     minimum host API version, u16
10     2     flags, u16: bit 0 = deterministic-strict (always 1 in v1.0),
             bit 1 = debug sections present
12     4     section count, u32
16     8     source hash, u64 (XXH64, HashBytes64 from core/hash/ContentHash.h,
             over the UTF-8 bytes of the root source file concatenated with
             each imported file in import-declaration order)
24     8     bytecode hash, u64 (XXH64 over the payload bytes of sections with
             kind < 0x40, concatenated in section-table order)
```

### Section table

`section count` records of 12 bytes each, immediately after the header:

```
u32 kind, u32 offset (from file start), u32 size (payload bytes, unpadded)
```

Section kinds (kind >= 0x40 is strippable; stripping removes the section and
its table entry and does not change the bytecode hash):

| Kind | Name | Payload |
|------|------|---------|
| 0x01 | StringTable | u32 count; count x u32 byte offsets (into the blob); u32 blob size; blob (UTF-8, not null terminated) |
| 0x02 | ConstPool | u32 count; count x u64 raw slot values |
| 0x03 | Code | u32 word count; words |
| 0x04 | Functions | u32 count; count x { u32 name (string idx), u32 code offset (word index into Code), u32 word count, u8 frame size, u8 arg slot count, u8 ret slot count, u8 flags (bit 0: state callback) } |
| 0x05 | Components | script-defined component schemas: u32 count; count x { u32 name (string idx, unprefixed: "HookshotState"), u16 field count, fields: { u32 name (string idx, dotted leaf), u8 scalar kind (FieldScalar order: Bool=0, Int32=1, UInt32=2, Float=3, Double=4, Color3=5; plus 16=Entity, 17=TagId, 18=Int64), u8 array count (1 for scalars), u16 byte offset, u64 default raw } } |
| 0x06 | BindComponents | u32 count; count x u32 component name (string idx). Index in this table = component-bind index (CHAS, commands) |
| 0x07 | BindFields | u32 count; count x { u32 component name (string idx), u32 field path (string idx, dotted), u8 expected scalar kind, u8 expected slot count, u16 reserved }. Index = field-bind index (CLD/CST/CLDX/CSTX) |
| 0x08 | BindTags | u32 count; count x u32 tag name (string idx). Index = tag-bind index |
| 0x09 | BindAssets | u32 count; count x { u32 path (string idx), u8 asset kind (cue=1, prefab=2, generic=3), u8 pad[3] } |
| 0x0A | HostImports | u32 count; count x { u32 name (string idx, e.g. "physics.raycast"), u32 signature (string idx, encoding in section C), u16 min host API version, u16 reserved } |
| 0x0B | Params | u32 count; count x { u32 name (string idx), u8 scalar kind, u8 slot count, u16 reserved, u64 default raw per slot x slot count } |
| 0x0C | Declarations | u32 count; count x { u32 name (string idx), u8 kind (ability=1, behavior=2, trigger=3, interaction=4), u8 state count, u16 callback count, states: state count x u32 name (string idx), callbacks: callback count x { u32 name (string idx: "start", "Pulling.fixed", ...), u32 function index } } |
| 0x0D | BindHostEnums | u32 count; count x { u32 const-pool index, u32 enum name (string idx), u32 member name (string idx) }. The cook writes a zero placeholder at that pool index; the loader patches the in-memory pool copy with the engine-resolved i32 value at link time. Unknown enum or member is a link error |
| 0x40 | LineTable | u32 count; count x { u32 code word offset, u32 line, u16 col, u16 file (string idx into DebugFiles) } sorted by code word offset |
| 0x41 | DebugFiles | u32 count; count x u32 path (string idx) |
| 0x42 | DebugSymbols | u32 count; count x { u32 function index, u32 name (string idx), u8 slot, u8 slot count, u16 reserved } local-variable names |

A loader must reject: bad magic, container version != 1, bytecode format
version != 0, section offsets or sizes outside the file, duplicate section
kinds, missing required sections (0x01-0x0C), or a bytecode hash mismatch.
Cook writes sections in kind order.

## C. Host API table

### Conventions

- Import names are dotted. The signature string encodes T types:
  `b` bool, `i` i32, `l` i64, `f` f32, `d` f64, `e` Entity, `t` TagId,
  `2` Vec2, `3` Vec3, `q` Quat, `C` component-bind index + component image,
  `A` asset id, `Sn` host record (n = record id below). Format:
  `<ret><args...>`, `v` for no return. Example: `physics.raycast` is
  `S1 33fi`.
- Arguments occupy the HCALL window in declaration order, each per its slot
  width. The return value is written starting at the window base.
- `f32` arguments and returns are f64 slots; the host rounds returns with the
  RNDF32 rule.
- Every call that takes an Entity traps `T_TRAP_ENTITY` on a dead handle.
  Additional traps listed per call. Non-finite Vec3/f32 inputs to physics and
  movement calls trap `T_TRAP_ARG`.
- Host enums: `QueryMask.<Name>` (the only host enum in v1.0) is an i32 whose
  member set is engine-registered; the value resolves at module link via the
  BindHostEnums fixup table (section B, kind 0x0D). Until engine query masks
  land (design doc prerequisite 7), the engine registers a single member set
  from game data and the raycast binding additionally filters by tag.

### Callback register prelude

The invoking bridge preloads the frame before entry; these are ordinary slots
the validator types from the callback's declaration kind:

| Kind | Prelude |
|------|---------|
| ability | r0 owner (entity), r1 tick (i64), r2 dt (f64), r3-r5 aim_origin (Vec3), r6-r8 aim_direction (Vec3) |
| behavior | r0 entity, r1 tick, r2 dt |
| trigger | r0 volume entity, r1 other entity, r2 tick, r3 dt |
| interaction | r0 instigator entity, r1 target entity, r2 tick, r3 dt |

`ctx.owner`, `ctx.tick`, `ctx.dt`, `ctx.aim_origin` and friends compile to
reads of these slots; they are not host calls.

### Host records

| Id | Name | Layout (slots) |
|----|------|----------------|
| S1 | RayHit | valid (bool), entity, position (3), normal (3), distance (f32): 9 slots |
| S2 | SweepHit | same layout as S1: 9 slots |
| S3 | OverlapHits | count (i32), entities [Entity; 16]: 17 slots |

### Imports (host API version 1)

Core intrinsics (imports 0-31, versioned with the VM, always present;
transcendentals use bundled deterministic kernels, fdlibm-derived, so results
are bit-identical across platforms and compilers; sqrt is IEEE-exact by
hardware):

| Idx | Name | Signature | Notes |
|-----|------|-----------|-------|
| 0 | core.sqrt | d d | IEEE sqrt; negative input yields NaN, no trap |
| 1 | core.sin | d d | deterministic kernel |
| 2 | core.cos | d d | deterministic kernel |
| 3 | core.abs_f | d d | |
| 4 | core.min_f | d dd | NaN propagates from either operand |
| 5 | core.max_f | d dd | |
| 6 | core.clamp_f | d ddd | clamp(x, lo, hi); traps T_TRAP_ARG if lo > hi |
| 7 | core.lerp_f | d ddd | a + (b-a)*t, evaluated exactly so |
| 8 | core.abs_i | i i | abs(INT_MIN) = INT_MIN |
| 9 | core.min_i | i ii | |
| 10 | core.max_i | i ii | |
| 11 | core.clamp_i | i iii | traps T_TRAP_ARG if lo > hi |
| 12 | core.vec3_normalize | 3 3 | zero vector traps T_TRAP_ARG |
| 13 | core.vec3_cross | 3 33 | |
| 14 | core.vec3_distance | d 33 | length(a - b) |
| 15 | core.vec2_length | d 2 | |
| 16 | core.vec2_normalize | 2 2 | zero vector traps T_TRAP_ARG |
| 17 | core.quat_rotate_vec3 | 3 q3 | |

Language builtins map: `sin/cos/sqrt/abs/min/max/clamp/lerp` dispatch by
static type to the f or i intrinsic; `distance/cross/normalize/length/dot`
dispatch by vector width (`dot`/`length` on Vec3 compile to VDOT3/VLEN3
opcodes, others to intrinsics). `pi` and `tau` are constant-pool f64 values
(0x400921FB54442D18 and 0x401921FB54442D18).

Host functions (imports 32+, host API version 1):

| Name | Signature | Semantics | Traps |
|------|-----------|-----------|-------|
| entity.alive | b e | generation-checked liveness | none (dead = false) |
| physics.raycast | S1 33fi | `PhysicsQueries::Raycast`; mask i32 (until engine masks land, tag-filtering per design doc prerequisite 7) | T_TRAP_ARG non-finite |
| physics.sweep_sphere | S2 3f3fi | sphere sweep | T_TRAP_ARG |
| physics.overlap_sphere | S3 3fi | first 16 hits in engine iteration order; count reports actual hits found, capped at 16 | T_TRAP_ARG |
| commands.add | v eC | enqueue AddComponentRaw with the marshalled image (slots to bytes per the component schema, f32 fields rounded) | T_TRAP_ENTITY |
| commands.remove | v eC | enqueue RemoveComponentRaw (component-bind index; no image) | T_TRAP_ENTITY |
| commands.destroy | v e | enqueue DestroyEntity | T_TRAP_ENTITY |
| commands.spawn | v A3 | enqueue a prefab spawn request at position (fire and forget in v1.0) | T_TRAP_ARG |
| movement.pull_toward | v 3ff | write the movement-intent component on the context subject (r0) | subject lacks movement components: T_TRAP_NO_COMPONENT |
| movement.impulse | v 3 | same subject rule | same |
| movement.halt | v | same subject rule | same |
| cue.fire | v A | append to the context subject's cue event buffer | buffer full: drop oldest, log once per script per session, no trap |
| cue.fire_at | v A3 | positional cue | same |
| random.f32 | f | next value in [0,1) from the callback's stream (section D.6) | |
| random.range | i ii | uniform integer in [lo, hi] inclusive | lo > hi: T_TRAP_ARG |
| random.unit_vec3 | 3 | uniform direction on the unit sphere | |
| ability.cancel | v | set pending outcome = cancel (section D.5); ability contexts only (validator enforces) | |
| ability.finish | v | set pending outcome = finish; ability contexts only | |
| debug.log | v <format const + window> | format string is a constant-pool string; args marshalled per the compile-time-checked format | |

Adding an import is append-only within a host API version; changing a
signature or removing an import bumps the version, and a module whose
`minimum host API version` exceeds the engine's is a load error.

## D. Semantics appendix

### D.1 Statement termination (lexer rule)

The lexer emits a statement terminator at a newline unless:

1. bracket depth > 0 (any of `(`, `[`, `{` opened by an expression context;
   a declaration or control-flow `{` body does not suppress terminators), or
2. the last significant token on the line is a binary operator, `,`, `=`, a
   compound-assignment operator, `.`, `->`, or an opening bracket, or
3. the next line's first significant token is `.` or a binary operator
   (`+ - * / % && || == != < <= > >=`): the expression continues.

Leading `-` on a continuation line is never a new unary-minus statement,
because an expression statement must be a call; any other bare expression is
a compile error. `return` followed by a terminator is a bare return; a value
must start on the same line. `;` is lexed as an explicit terminator;
consecutive terminators collapse. `else` may begin a line (rules 1-3 do not
apply to it; the parser attaches it to the preceding `if`).

### D.2 Numerics

- Integer literals are i32; there are no literal suffixes. An i64 arises only
  from `ctx.tick` or `i64(x)`. Out-of-range literals are compile errors.
- Float literals are f32 by default (the f64 slot holds the nearest-f32
  value); a literal in an f64 context (annotated) is f64.
- i32 and i64 arithmetic wraps (two's complement). Integer divide and
  remainder by zero trap. `INT_MIN / -1` yields `INT_MIN`; `INT_MIN % -1`
  yields 0.
- Float arithmetic is IEEE 754 binary64, round-to-nearest-even, evaluated in
  source order with no reassociation, no FMA contraction, and no fast-math
  (the interpreter is compiled with strict float semantics for these paths).
- `f32` semantics: values round through RNDF32 at explicit `f32(x)`
  conversions and at component stores to f32 fields. Everything else stays
  f64.
- Float compares: NaN makes `==`, `<`, `<=`, `>`, `>=` false and `!=` true.
- No implicit numeric conversions anywhere. Mixed-type arithmetic is a
  compile error naming the conversion to insert.
- Conversion syntax: `i32(x)`, `i64(x)`, `f32(x)`, `f64(x)` are calls whose
  callee is a type keyword. Grammar note: a type keyword is a valid `primary`
  only when immediately called; the design doc EBNF's `primary = ... | IDENT`
  is extended by this rule.
- `for i in a..b` is half-open [a, b), i is i32, a and b evaluate once at
  loop entry; if a >= b the body does not run.

### D.3 Records and components

- A record literal must name only declared fields; unlisted fields take the
  component-declared defaults; a field with no declared default defaults to
  zero (all scalar kinds; Entity defaults to the invalid id; TagId to 0).
- Duplicate field names in a literal are compile errors.
- Component field access on an entity that lacks the component traps
  `T_TRAP_NO_COMPONENT` (guard with `has`).
- Script component names are declared unprefixed (`HookshotState`) and get
  identity `MakeComponentTypeId("script.HookshotState")`. Two scripts
  declaring the same component name must declare identical schemas (field
  names, kinds, offsets); otherwise module load fails naming both scripts.

### D.4 State machines

- `state` declares a name; state indices are declaration order within the
  block.
- The current state is persistent data: behaviors store it in the engine
  `ScriptBehavior` component; abilities store it in `ScriptAbilityState`
  (script handle plus state, on the owner entity, added by the ability bridge
  at activation and removed at finish/cancel). Both store the state as the
  interned name, not the index, so hot reload matches by name.
- `enter X` sets the pending transition; the last `enter` executed in a
  callback wins. The transition applies after the callback returns:
  `CurrentState.exit` runs, then `X.enter`, in that order, on the same tick,
  and both are ordinary callbacks (they may not `enter`; the validator rejects
  ENTER in enter/exit callbacks).
- `ability.cancel()` and `ability.finish()` set a pending outcome that
  overrides any pending `enter`. Execution continues until the callback
  returns; then `cancel` or `finish` runs and the instance ends. If both are
  called, the last call wins.
- A trap behaves as `cancel` for abilities and as "skip to next tick" for
  behaviors, triggers, and interactions (the callback's writes up to the trap
  point stand; command-buffer entries enqueued before the trap stand).

### D.5 Deterministic callback order

Within one fixed tick, each bridge invokes its callbacks in a defined order:

- Ability callbacks: `AbilityActivationQueue` order for `start`; for per-state
  `fixed`, ascending (owner entity index, ability id) over active script
  abilities.
- Behavior callbacks: the `ScriptBehaviorSystem` cached query's chunk order
  (archetype registration order, then chunk index, then row).
- Trigger callbacks: the overlap diff sorted ascending by (volume entity
  index, other entity index); all `on_exit` for a tick run before all
  `on_enter`.
- Interaction callbacks: input-intent queue order.

Bridges run as scheduled systems ordered by `EngineSchedule::After` edges;
their relative order is fixed at registration (ability, then behavior, then
trigger, then interaction) and asserted by a schedule test.

### D.6 Random streams

`core/random` provides PCG32 (state 64, increment odd 64). A callback's
stream seeds from `Mix(world seed, subject entity index and generation,
declaration name hash, TickIndex)` where Mix is SplitMix64 over the
concatenation. Draws within one callback advance the stream; the stream does
not persist across callbacks (re-seeded per callback), so replay needs no VM
state, per principle 15.

### D.7 Trap catalog

| Code | Name | Raised by |
|------|------|-----------|
| 1 | T_TRAP_BUDGET | instruction budget exhausted |
| 2 | T_TRAP_CALL_DEPTH | CALL beyond depth 32 |
| 3 | T_TRAP_DIV_ZERO | DIVI, MODI |
| 4 | T_TRAP_CONV | F2I from NaN or out of range |
| 5 | T_TRAP_BOUNDS | CLDX, CSTX, AGETL, ASETL index out of range |
| 6 | T_TRAP_ENTITY | dead entity handle at a component, tag, or host op |
| 7 | T_TRAP_NO_COMPONENT | CLD/CST on absent component; movement call on a subject without movement components |
| 8 | T_TRAP_TAG_CAPACITY | TADD with no or full GameplayTagContainer |
| 9 | T_TRAP_ARG | host call argument contract violation (non-finite, lo > hi, zero-length normalize) |

Every trap reports (script, declaration, callback, source line via the line
table, trap code, tick). The message shape is
`script hookshot.t: Hookshot.Pulling.fixed:23: trap NO_COMPONENT (HookshotState) at tick 481`.

### D.8 Imports

An import path resolves relative to the importing file's directory. `..`
segments and absolute paths are compile errors. Import cycles are compile
errors naming the cycle. Importing the same file twice (directly or
transitively) is idempotent. The import closure is what the header's source
hash covers (section B), in import-declaration order, depth first.

## E. Milestone checklists

Milestone 1 (examples plus host API) is discharged by this spec and the
fixture corpus in `test/script/fixtures/`. Each task below names its files
and its done-when test; tasks are ordered and independently verifiable. Test
suites live in a new `test/script/` group added to `test/CMakeLists.txt` with
the same stanza shape as `jobs_tests`.

### Milestone 2: bytecode plus VM core

1. `engine/include/script/ScriptBytecode.h`: opcode enum with the exact hex
   values from section A, encode/decode helpers, format tags.
   Done when: a static_assert pins the opcode count (61) and a decode test
   round-trips every format.
2. `engine/include/script/ScriptModule.h` + `engine/src/script/ScriptModule.cpp`:
   in-memory module and the `.tbc` parser per section B. No engine
   dependencies beyond `core/hash`.
   Done when: a golden `.tbc` byte fixture parses; each malformed-header and
   malformed-section case from section B's reject list is rejected with the
   right error.
3. `engine/src/script/ScriptModuleWriter.cpp` (cook-gated later; plain code
   now): serializes a module back to `.tbc`.
   Done when: parse(write(m)) == m for a hand-built module and the bytecode
   hash is stable.
4. `engine/include/script/ScriptBytecodeValidator.h` + src: structural rules
   1-6, then type rules 7-10 from section A.
   Done when: a valid hand-assembled module passes; one corrupted fixture per
   rule is rejected naming that rule.
5. `engine/include/script/ScriptVm.h` + src: interpreter loop, trap model
   (section D.7), per-instruction budget, callback preludes (section C).
   Done when: hand-assembled semantics tests cover every opcode (including
   wrap, NaN, and trap edges from D.2) and a budget test traps at the exact
   configured count.
6. `engine/src/script/ScriptIntrinsics.cpp`: imports 0-17 with deterministic
   kernels.
   Done when: golden bit-pattern tests (exact u64 expected values) pass for a
   sample grid including edge inputs.
7. Determinism harness in `test/script/`: run a module N times and across two
   VM instances, compare per-callback write logs and instruction counts.
   Done when: identical, and a deliberately nondeterministic fake host import
   is caught by the harness (negative control).

### Milestone 3: compiler front end

1. `engine/src/script/compiler/Lexer.cpp`: tokens plus the D.1 termination
   rule. Done when: a termination-rule table test (each D.1 clause, positive
   and negative) passes.
2. `Parser.cpp`: AST per the design doc EBNF. Done when: all five fixtures
   parse; error-recovery test resumes at the next declaration.
3. `Typecheck.cpp`: D.2/D.3 rules, explicit-conversion errors, callback
   signature checks per declaration kind. Done when: golden error tests (bad
   source in, expected file:line:col and message shape out) pass.
4. `Codegen.cpp`: AST to bytecode v0. Done when: golden disassembly tests for
   the five fixtures match checked-in `.disasm` files, and every generated
   module passes the validator.
5. `ScriptDisassembler.cpp` (feeds the golden tests and the future
   `script.disasm` command). Done when: disassembly of the golden fixtures is
   byte-stable.

### Milestone 4: asset, cook, and hot reload

1. `engine/src/assets/cook/ScriptImporter.cpp` (`SENCHA_ENABLE_COOK`):
   extension `.t`, compiles via the milestone-3 compiler, emits `.tbc`,
   import-closure hash in the cook record. Done when: an ImportOnDemand test
   cooks a fixture and a library edit invalidates its dependent.
2. `ScriptAssetLoader` + `ScriptCache` under `engine/{include,src}/assets/script/`:
   `LoadStaged` parse+validate (pure), `CommitTyped` link (bind tables per
   design doc) inside the pre-population window only. Register `.tbc` in
   `AssetTypeFromExtension`, cache member in `RuntimeAssets`, methods plus
   `LoaderFor` case in `AssetSystem`. Done when: the audio-loader test shape
   passes for scripts, and a post-population load attempt fails with the
   window error.
3. Link failures: missing tag, component, field, asset, host import each
   produce the section-C/D error naming the symbol. Done when: one test per
   failure class.
4. Hot reload: watcher learns `.t`; same-layout swap via `CommitReload` /
   `ReloadInPlace`; layout change detected by component schema hash routes to
   the world-reload path with save/load migration. Done when: both paths have
   tests (state preserved across live swap; field add/remove/retype migrated
   across world reload; vanished state cancels).

### Milestone 5: bridges plus prerequisite engine work

1. `World::RegisterComponentRaw` + `ScriptComponentSerializer`
   (`engine/include/world/serialization/`): non-template registration and
   serializer synthesized from a section-B component schema. Done when: a
   script component registers, round-trips save/load, and appears in
   `RuntimeFields()` identically to an equivalent native component.
2. `CommandBuffer::AddComponentRaw` / `RemoveComponentRaw`. Done when: flush
   order and lifecycle-hook behavior match the templated path in tests.
3. `core/random/DeterministicRandom.h`: PCG32 + SplitMix64 mix per D.6.
   Done when: golden sequence tests pass.
4. `AbilityDefinition` script ref + `AbilityScriptBridge` + `ScriptAbilityState`
   component. Done when: an activation drives `start`/`fixed`/`finish` in
   D.5 order in a schedule test.
5. `ScriptBehaviorSystem` (`engine/include/script/`). Done when: spawn/fixed/
   despawn fire in chunk order; `ScriptBehavior` state persists.
6. `TriggerVolumeSystem` + bridge; `InteractionSystem` + bridge. Done when:
   D.5 ordering tests pass (exit-before-enter; sorted pair order).
7. Fitness scripts `scripts/check_script_layering.sh` and
   `scripts/check_script_host_surface.sh`, wired as ctests. Done when: both
   pass on the tree and each fails on a seeded violation in a scratch copy.
8. Hookshot end to end (`test/script/` or `test/framework/`): tags granted,
   activation queued, ticks run, component/tag/cue trajectory asserted from
   the cooked fixture. Done when: green, plus the scripted determinism gate
   (serial vs parallel engine config, per-tick component hash equality).
