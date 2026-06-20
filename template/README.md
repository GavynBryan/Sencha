# Sencha game template

A minimal Sencha game project. It builds **only** a game module (`game.so`)
against a prebuilt Sencha SDK: no engine source is present or required. This is
the supported way to make a game on Sencha.

```
project.senchaproj   project descriptor the editor opens (name, module, content)
CMakeLists.txt       find_package(Sencha) + sencha_game_module(game ...)
src/                 the game module: TemplateGame (a v4 module-is-a-Game)
assets/              authored content; the cook writes assets/.cooked/
```

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

The editor reads the descriptor, loads `game.so` for its component serializers
(so it can edit scenes containing the game's components), and can launch the
project in `app`.

## Play it directly

The host discovers `game.so` beside itself, or takes `--game`. Run from the
project directory (its CWD is the content root):

```sh
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/<name>
```

Cook a level first (editor Cook, or the cook tooling) so
`assets/.cooked/levels/<name>.cooked.json` exists.
