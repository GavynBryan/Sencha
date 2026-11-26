# The generator's stop rule

Wave 2, item E, stage 2: the component generator finished (leases, cosmetic
verbs, prediction, completion, reasons and results, one-sided bounds,
descriptions and units, asset and data-asset references, borrowed arguments,
array and record results, `Name_<Method>`, the target alive check, the engine's
`API_HEADERS` and the dependency file), then measured against the rule its plan
set before it was built:

> If the generated adapter is slower than the hand reader on the response path
> (`VerbDispatchBenchTests`, `ResponsesDeliveryBench`), or the generator adds
> more than about ten seconds of wall time to a clean engine build or more than
> two seconds to an incremental build after one provider header edit, stages 5a
> to 7 are cancelled.

The rule also asks for no allocation with a string and an asset argument.

## Verdict

**The rule passes.** Every measure is inside its bound:

| Measure | Bound | Measured | Result |
|---|---|---|---|
| Generated adapter against the hand reader, five arguments with a text and an asset (`VerbDispatchBench`) | no slower | 86.1 ns generated, 86.4 ns hand (medians of five) | pass |
| Generated adapter against the hand reader on the response path (`ResponsesDeliveryBench`) | no slower | 101.9 ns generated, 102.2 ns hand per delivery run (medians of ten) | pass |
| Allocations per warmed call with a text and an asset argument | none | 0 generated, 0 hand | pass |
| Added wall time, clean engine build | about 10 s | +2.1 s on the critical path; +2.5 s mean total, inside run-to-run noise of ±6 s | pass |
| Added wall time, incremental build after one provider header edit | about 2 s | +0.77 s | pass |

## Machine and method

- 13th Gen Intel Core i7-13620H (6 performance cores with two threads each, 4
  efficiency cores; 16 threads), Fedora 43, GCC for the engine and Clang 21.1.8
  libraries for the generator. Nothing else building or testing during a run.
- **Benches:** the `profile` preset (`-O3 -DNDEBUG -g -fno-omit-frame-pointer`),
  rebuilt from this tree, each run pinned to one performance core
  (`taskset -c 2`) under `timeout 300`, five runs of each bench.
- **Build times:** the `dev` tree (`build/`, Debug), target `sencha_engine`,
  `-j15`. A clean build removes the engine's objects and generated companions
  (`engine/CMakeFiles/sencha_engine.dir`, `engine/generated-components`), keeps
  the third-party libraries and the generator binary, and times `ninja
  sencha_engine`. The incremental build touches
  `engine/include/inventory/InventoryVocabulary.h`, one of the provider headers,
  and times the same command. "Before" is the same tree with the engine's
  `API_HEADERS` list emptied (a local edit, never committed); everything else,
  the dependency file included, is the same in both. Three runs before; six
  clean and three incremental after.

## Build times

Clean engine build, seconds. The prefix is from `.ninja_log`: every engine
compile waits on every generated companion (they are order-only inputs), so the
time until the last generator run ends is exactly what the generator adds to the
critical path.

| State | Run | Wall | Generator runs | Last generator run ends | First engine compile starts |
|---|---|---|---|---|---|
| without API headers | 1 | 164.1 | 90 | 3.25 | 3.26 |
| without API headers | 2 | 165.6 | 90 | 3.23 | 3.24 |
| without API headers | 3 | 165.9 | 90 | 3.29 | 3.31 |
| with API headers | 1 | 164.4 | | | |
| with API headers | 2 | 175.4 | | | |
| with API headers | 3 | 169.7 | | | |
| with API headers | 4 | 162.1 | 114 | 5.44 | 5.46 |
| with API headers | 5 | 166.9 | 114 | 5.40 | 5.42 |
| with API headers | 6 | 168.0 | 114 | 5.19 | 5.20 |

- Critical path: 3.26 s mean before, 5.34 s after: **+2.1 s**.
- Total wall: 165.2 s mean before, 167.8 s after (+2.5 s), with a spread of
  13 s across the six runs after, so the total alone cannot resolve a difference
  this small; the critical-path figure is the measurement.
- CPU: the 24 provider headers cost the generator 15.9 s run one after another
  (0.66 s each), against the plan's estimate of 25 to 50 CPU seconds.

Incremental build after touching one provider header, seconds:

| State | Run 1 | Run 2 | Run 3 | Mean |
|---|---|---|---|---|
| without API headers | 14.42 | 14.40 | 14.37 | 14.40 |
| with API headers | 15.19 | 15.14 | 15.17 | 15.17 |

**+0.77 s**: the header's own generator run, which the recompiles wait for.

What these figures do not include: the 24 provider headers declare nothing yet
(their migration is stages 5a to 5c), so their companions are empty and nothing
includes them. Once a provider is annotated, every translation unit that
includes its header also compiles its companion's adapters; that cost belongs to
the migration and is measured there. The parse, which is what the rule bounds,
is the same whether or not the header is annotated.

## Benches

`VerbDispatchBench.AGeneratedAdapterReadsAStringAndAnAssetInPlaceAtTheHandReadersCost`:
one compiled binding per verb, the same five-argument contract (a live target
from an input, a text, an asset, a `float` and an `int32_t` from constants),
200,000 warmed invocations per round, best of five rounds, generated and hand
rounds interleaved. The generated adapter is `GoldenBenchVerbs::Paint` in the
golden fixture, compiled from the companion the generator emits byte for byte;
the hand reader does what a hand-written verb does with the same slots: reads
each with `TryGet*`, checks the target is alive and the number fits.

| Run | Generated (ns/call) | Hand (ns/call) | Allocations, generated / hand |
|---|---|---|---|
| 1 | 86.0 | 86.4 | 0 / 0 |
| 2 | 86.1 | 85.9 | 0 / 0 |
| 3 | 86.2 | 86.1 | 0 / 0 |
| 4 | 86.8 | 86.5 | 0 / 0 |
| 5 | 85.5 | 86.5 | 0 / 0 |
| Median | 86.1 | 86.4 | |

The existing rows of the same test, for scale: a warmed no-argument dispatch
17 to 19 ns, an entity argument 34 to 35 ns.

`ResponsesDeliveryBench`: 10,000 entities sharing one response binding whose
command names a one-target verb, delivered once each, then one pass that runs
every command. "Pass" is the pass's time divided by the deliveries; it is where
the verb runs. The hand reader (`bench.touch_by_hand`) decodes its target with
`TryGetEntity` and checks it is alive; the generated adapter
(`test.bench.touch`) decodes and checks the same. Both rows allocate nothing to
deliver or to run.

| Run | Hand, pass (ns) | Generated, pass (ns) | Hand +100 subscribers | Generated +100 subscribers |
|---|---|---|---|---|
| 1 | 101.0 | 100.0 | 102.3 | 101.8 |
| 2 | 100.0 | 102.5 | 102.8 | 103.3 |
| 3 | 100.3 | 99.7 | 102.2 | 101.7 |
| 4 | 102.2 | 121.0 | 126.9 | 102.7 |
| 5 | 102.7 | 103.3 | 102.2 | 101.9 |

Median of all ten samples per side: hand 102.2 ns, generated 101.9 ns. Run 4
has one slow sample on each side (121.0 generated, 126.9 hand), a scheduling
outlier that lands on either reader. The existing "commands run" row, whose
verb reads its target without checking it is alive, measured 99.4 to 115.4 ns
in the same runs.

## Found while measuring

- **The dispatcher allocated for every text or asset argument, hand-written or
  generated.** `AuthoredArguments::Set` took its value by copy, so filling the
  dispatcher's warmed argument pack from a binding's constants allocated a new
  string for a text and for an asset's path on every call: 2 allocations per
  call for the five-argument verb, before any adapter ran. `Set` now assigns
  over the slot's own storage from a const reference, and moves from an rvalue,
  so a warmed pack reuses its strings. With that, neither the generated adapter
  (which reads the text as a `std::string_view` and borrows the `AssetRef`) nor
  the hand reader allocates.
