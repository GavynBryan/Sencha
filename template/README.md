# Sencha game template

A minimal Sencha game project. It builds **only** a game module (`game.so`)
against a prebuilt Sencha SDK: no engine source is present or required. This is
the supported way to make a game on Sencha.

```
project.senchaproj   project descriptor the editor opens (name, module, content)
CMakeLists.txt       find_package(Sencha) + sencha_game_module(game ...)
src/TemplateGame.*   the game module: a v4 module-is-a-Game (map viewer + systems)
src/SpinComponent.h  an example game-defined component (schema-reflected)
assets/              authored content; the cook writes assets/.cooked/
```

`SpinComponent` shows the whole game-data path: a `TypeSchema` makes it cook
through and appear, editable, in the editor inspector with no editor code naming
it, and `SpinSystem` (in `TemplateGame.cpp`) rotates entities that carry it at
play time. Delete it and add your own.

## Build the game module

Point CMake at the installed SDK (the directory you passed to
`cmake --install`):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
```

This produces `build/game.so`. `find_package(Sencha 0.1)` resolves the engine,
the editor, and the `app` host from the SDK; nothing in this project rebuilds
the engine.

## Open it in the editor

```sh
SENCHA_PROJECT=$PWD/project.senchaproj /path/to/sencha-sdk/bin/sencha_editor
```

The editor reads the descriptor and loads `game.so` for its component
serializers (so it can edit scenes containing the game's components). The
author -> cook -> play loop runs from the toolbar (Cook / Play / Stop) or the
console:

```
cook [name]    bake the live level into assets/.cooked/levels/<name>
play [map]     launch the project in app (PIE); defaults to the last cooked map
stop           end the play session
```

The cook cell size is the `editor.cook.cell_size` cvar. Place a plain entity
from the Hierarchy panel's "New Entity" button, then add game components to it
in the Inspector.

## Play it directly

The host discovers `game.so` beside itself, or takes `--game`. Run from the
project directory (its CWD is the content root):

```sh
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/<name>
```

Cook a level first (editor Cook, or the cook tooling) so
`assets/.cooked/levels/<name>.cooked.json` exists.
