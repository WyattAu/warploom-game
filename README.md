# warploom-game

The first external consumer of the [warploom](https://github.com/WyattAu/warploom)
engine — and the proof that the engine is consumable as a package.

## What it is

A minimal headless game: a tower of 20 simulated cubes drops onto the ground
plane under fixed-step integration. The `drop` binary checks three claims and
exits 0 iff all hold:

1. **Replay reproduces** — session A captures (spawns + 600 ticks), session B
   loads the file fresh; the documents are byte-identical.
2. **Determinism is external** — sessions C and D, set up identically and
   ticked identically, hold bit-identical body positions.
3. **The sim actually ran** — the pile fell (top 5.9 → ~3.5) and rests on the
   plane (bottom ≈ 0.5). This guards a no-op sim passing claims 1 and 2
   vacuously.

The game is the *host* (the role the viewport and `headless_host` play):
Steps are executed as `session.tick()` calls here, then forwarded to the
session for recording. Bodies are setup, not edits, so claim 1 compares
documents rather than whole snapshots — physics re-simulation on load is
still the engine's open item (warploom roadmap C3), not something this game
papers over.

## What it proves

- `find_package`-style consumption works: the game reaches the engine only
  through public headers (`warploom/core/...`, `warploom/physics/...`). If a
  future change needs an engine-internal header here, that is an engine API
  gap, and the fix belongs in warploom.
- The estate's shared C++ CI (`cpp-kit.yml`) works outside warploom: this
  repo calls it with the warploom-specific legs disabled (no Vulkan sources,
  no socket harness, no docs script) and the examples leg pointed at `drop`.
- The engine's install is honest: this repo found and fixed two packaging
  defects (a leaked internal target name in the physics export, a missing
  transitive dependency in the core package config).

## Build and run

```sh
cmake --preset default
cmake --build build/default -j$(nproc)
ctest --test-dir build/default --output-on-failure
./build/default/bin/drop
```

`FetchContent` pulls warploom from `main`; every other preset in
`CMakePresets.json` exists for the shared CI matrix.
