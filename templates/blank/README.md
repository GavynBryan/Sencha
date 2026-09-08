# Blank template

A game module with every hook empty, and one scene with nothing in it. The
engine boots, mounts `assets/`, loads `levels/empty` when asked, runs frames,
and shuts down; nobody is admitted, nothing is looked through, nothing moves.

Start here when none of the other templates is your game.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/empty
```

Add components in `OnRegisterComponents`, systems in `OnRegisterSystems`, and
whatever your game decides a loaded level means to it in a system that reads
`ZoneResidencyContext`.
