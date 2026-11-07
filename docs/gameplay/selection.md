# Selection tables

A selection table answers one question: of the options a querier has, which
does it take now? It is the option-level sibling of candidate evaluation
(`docs/spatial/candidates.md`): candidates rank places and entities, a table
ranks courses of action. Neither moves anything. The caller reads the result
and acts through abilities, navigation and verbs.

Code: `engine/include/logic/selection/`, `engine/src/logic/selection/`.
Tests: `test/core/SelectionTableTests.cpp`.

## What it owns and what it does not

It owns scoring options by weighted response curves, disqualifying them by
requirements, holding the current option through a commitment or a switch
margin, and a seeded pick among near-equal options. It does not run the option,
schedule itself, remember anything between runs, or replicate.

## Authored form

A `selection.table` `.sdata` asset.

```json
{
  "name": "ranged",
  "entities": ["threat"],
  "inputs": ["threat_distance", "position_quality"],
  "switch_margin": 0.15,
  "selection": { "mode": "pick_from_band", "band_width": 0.05 },
  "interrupt": { "any": ["state.hit"] },
  "options": [
    { "name": "hold", "commit_seconds": 1.5,
      "considerations": [
        { "query": "perception.sees", "entities": { "observer": "querier", "subject": "threat" },
          "require": { "expect": true } },
        { "input": "threat_distance",
          "score": { "curve": { "shape": "band", "low": 6, "preferred_low": 10, "preferred_high": 22, "high": 30 },
                     "weight": 2 } } ] },
    { "name": "reposition",
      "considerations": [
        { "input": "position_quality",
          "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0.2, "high": 0.8 } } } ] }
  ]
}
```

- `entities` names the entities the caller binds after the querier, which is
  always entity 0 and named `querier`. `inputs` names numbers the caller
  supplies, for values it already computed.
- A consideration reads exactly one of an `input` or a `query`. A query's
  `entities` map its Entity arguments to table entities, and `arguments` holds
  its constants, compiled against the query's declaration when the table binds.
  The query must answer Bool, Int or Float.
- A consideration has exactly one of `require` (`min`/`max`, or `expect` for
  0/1) and `score` (`curve` and `weight`). An input or a non-Bool query needs a
  curve to score. Curves are `math/ResponseCurve.h`, normalized against
  authored bounds, never against the other options.
- A value that cannot be measured (an Unavailable query, an invalid entity)
  disqualifies its option unless `unmeasured.score` is given; that value then
  scores, or passes a requirement.
- An option with no scored considerations scores 1 when it qualifies.

Loading has the same two stages as candidates: `CompileSelectionTableDesc` in
the load stage with no World, then `BindSelectionTable` on the owner thread,
which resolves queries and interrupt tags. `SelectionTableBinding` holds the
asset and rebinds when it reloads or the query catalog or tag vocabulary
moves.

## Running

`SelectionEvaluator(world, queries).Evaluate(table, context, scratch, trace)`
is a pure read on the owner thread. The context carries the entities and
inputs in table order, the current option and how long it has held, a mask of
options the caller rules out (one whose execution has nowhere to go, say), and
a seed.

1. An option the caller ruled out is disqualified before anything is asked.
   Each other option's requirements run first; the first failure disqualifies it and
   skips the rest of its considerations.
2. A qualified option's score is the weighted mean of its curve outputs,
   summed in authored order.
3. The current option, if it still qualifies and the querier's tags do not
   match `interrupt`, is kept while `CurrentSeconds < commit_seconds`.
4. Otherwise the best option wins, ties to the earlier one, unless the current
   option qualifies and the best does not beat it by `switch_margin`.
5. With `pick_from_band`, the winner is drawn among qualified options scoring
   at least best minus `band_width`, by the smallest `SeededPickKey(seed,
   option)`, the same draw candidate evaluation uses.

The result names the option, its score, and why it won: `Chosen`,
`Committed` or `WithinMargin`. A context with too few entities or inputs, or a
current option the table lacks, is `ContextMissing`. A `SelectionTrace` records
every option's qualification, the consideration that disqualified it, and each
consideration's raw value, status and curve output.

Commitment and timing are in seconds the caller measures, so an actor whose
time is dilated measures its own.

## Determinism and authority

A run's result is a pure function of the bound table, the context and the
World as the queries see it. A table belongs wherever its caller has
authority; nothing about a run replicates.
