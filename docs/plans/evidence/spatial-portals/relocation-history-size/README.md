# Relocation: transform history size and read cost

Question: `WorldTransformHistory` grew from 84 bytes (`main`) to 116 bytes with
the relocation crossing (`Crossing*` fields and the `HistoryState` lifecycle).
Mesh and shadow-caster extraction resolve one history per drawn instance that
carries one. What does a read cost now?

## Method

No existing bench exercises the history path: the render scale bench scenes are
static meshes, which take the history-free extraction query, and
`WorldTransformHistory` cannot be authored into a scene. An end-to-end A/B
against a `main` build in another worktree was not run (the session may not run
binaries in sibling worktrees).

Instead, `history_bench.cpp` (this directory) iterates the same chunked
`Query<Read<History>>` extraction uses, in one binary compiled at `-O2`
against the branch's headers, interleaving per pass:

- **main**: an 84-byte copy of `main`'s layout resolved with `main`'s
  `ResolvePresentationPose` (interpolate only);
- **branch**: the 116-byte `WorldTransformHistory` resolved with the branch's
  `ResolvePresentationPose` (interpolate, then the state check);
- **cold-path**: the branch, with the crossing branches moved out of line;
- **layout**: the 116-byte layout resolved with `main`'s function, isolating
  the size from the state check.

Every entity is `Steady`, the common case. 300 passes per run, pinned to one CPU
(`taskset -c 2`), three runs each at 10 000 and 100 000 entities. Raw output is
in `runs.txt`.

    c++ -std=c++20 -O2 -DNDEBUG -mavx2 -mfma -I engine/include \
        -I build/engine/generated-public -I build/engine/generated-components/sencha_engine \
        history_bench.cpp -o history_bench -L build/engine -lsencha_engine -Wl,-rpath,$PWD/build/engine
    taskset -c 2 ./history_bench 100000 300

Machine: Intel Core i7-13620H, 16 threads. Load average 2.4 to 2.6 (1 minute)
during the runs, down from 45 earlier in the day while another session ran its
suite; runs within one invocation agree to about 1%.

## Result

| entities | main median | branch median | branch / main | layout median | per instance (main → branch) |
|---|---|---|---|---|---|
| 10 000 | 0.177 ms | 0.203 ms | 1.15 | 0.177 ms | 17.6 → 20.2 ns |
| 100 000 | 1.89 ms | 2.18 ms | 1.15 | 1.89 ms | 18.9 → 21.7 ns |

- The size costs nothing measurable: the 116-byte layout read with `main`'s
  function matches `main` within 1%, in cache (10 000) and out of it (100 000).
- The state check costs about 2.6 ns per instance, roughly 15% of the resolve.
  Moving the crossing branches out of line recovers almost none of it (0.198 ms
  against 0.203 ms at 10 000), so the cost is the per-row state test itself, not
  code size.
- At 10 000 history-carrying instances that is about 0.026 ms per extraction
  pass, run once for meshes and once for shadow casters.

These are micro-benchmark numbers for the resolve loop alone, not frame times;
the engine's dev build is Debug, so its own extraction is far slower than this
and dominated by other work.

## Decision

No change. The size the review asked to reduce is not the cost; the cost is the
branch that presents a crossing at all, and every reader has to take it for the
relocation to draw correctly. If history-carrying instance counts reach the
range where 2.6 ns each shows in a capture, the remedy is to keep crossing
histories out of the per-instance loop (resolve those few separately), not to
shrink the component.
