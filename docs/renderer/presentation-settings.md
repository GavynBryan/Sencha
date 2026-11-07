# Presentation settings

How every view looks, authored as data in layers rather than written as a game's C++. Today
that is bloom; the record (`render/PresentationSettings.h`) is where further authored looks join.

## Layers

Each layer sets only the fields it changes and inherits the rest from the layer below it.
Lowest first:

| Layer | Authored in | Edited in Kyusu |
|---|---|---|
| Engine default | `PresentationSettings{}`: bloom off | — |
| Game | `asset://data/presentation.sdata`, data subtype `presentation.settings` | the data editor |
| World | the world manifest's `presentation` | the partition outline, World picked |
| Graph | a graph record's `presentation` | the partition outline, the graph picked |
| Scene | a scene's `settings.presentation` | nothing selected in a level that is not a world; the zone picked in a world |

A level that is not a world resolves the game's layer and then its scene's. In a world, the
layers in force are the game's, the world's, then the focus zone's graph's and the focus
zone's scene's. A world scene carries no layer, because the world's own lives in its manifest;
the world cook warns when one sets any.

```json
{ "bloom": { "enabled": true, "threshold": 1, "intensity": 0.8, "spread": 1 } }
```

A region dims the game's bloom by setting only `intensity` on its graph, and one scene turns it
off with `"enabled": false`; neither repeats the other values. One schema,
`PresentationSettingsSchema()`, validates every layer, and Kyusu builds its rows from it: an
inherited row shows the value in force and the layer that sets it, and emptying a row (or
choosing Inherit) gives the field back to the layer below.

## Where each layer lives at runtime

- The game's comes from its data asset, through `GamePresentationLayer`. A project without
  the asset has no layer, which is not an error, and a reload of the asset is followed.
- A world's and its graphs' come from the manifest `LoadedLevel` holds.
- A scene's is cooked into its `.smap` as the optional `PRES` section, which a scene that sets
  nothing leaves out. Attaching the scene puts it on its zone record (`ZonePresentation`), so
  it goes whenever the zone does: streamed out, or unloaded with the level. Nothing has to
  withdraw it, and nothing a scene sets survives the scene.

Scenes spawned at runtime, prefabs among them, carry no layer: they are not loaded as a zone.

## Resolving and publishing

`PresentationSettingsSystem` runs once a frame where a window presents. It resolves the layers
at `LoadedLevel::PlayZone()` with `PresentationLayersAt` and `ResolvePresentationSettings`,
but only when one of its inputs changed: the game's layer, the manifest, the play zone, or the
zone's layer. `PresentationSettingsPublisher` holds the result as one `sencha.bloom`
[post-effect contribution](post-effects.md) for every view, with a `Session` lifetime at
`kPresentationSettingsOrder`, below every order a game writes at. A game contribution at the
default order therefore wins outright, and one at weight 0 turns the authored bloom off for
as long as it is held. Bloom off in the resolved settings clears the contribution, so no pass
runs.

Resolution follows the primary focus. Crossing into a zone switches immediately; nothing
blends.

## Kyusu's viewports

Perspective viewports preview the focus scene's resolved settings through the engine's own
`PostEffectChain`, recorded between the scene and the output pass (`ViewportPostEffects`, one
chain per viewport, retired once the GPU is finished with it). Ortho views and debug channels
keep the plain working view, as they do for the sky. `editor.preview.post` turns the preview
off.
