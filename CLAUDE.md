# Working on k

A Source 1-style engine in C++20. Start with `docs/README.md`.

## Documentation rule

Every component is documented in `docs/components/`. When you add or change a component:

- Create or update its page in `docs/components/` in the same commit as the code: what it
  does, how to use it (console commands, map keys, file formats), and how it works.
- New entity classes go in the entity reference table in `docs/components/entities.md` and in
  `tools/trenchbroom/k.fgd`.
- New or changed cvars/commands: regenerate the reference
  (`cd build/bin && ./k -dedicated +cvar_markdown ../../docs/reference/cvars.md +quit`).
- Link new pages from `docs/README.md`; update `docs/ROADMAP.md` checkboxes.

## Build and test

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/bin/k_tests
scripts/smoke_test.sh build    # Linux; uses Xvfb with SDL_VIDEO_FORCE_EGL=1
```

## Conventions

- Namespace `k`, files `snake_case`, types `PascalCase`, functions `camelCase`.
- Z up, X forward, 1 unit ≈ 1 inch, angles in degrees (positive pitch looks down).
- Cvar prefixes: `sv_ cl_ r_ mat_ snd_ net_ host_`.
- New entity source files need a `linkEntities_<file>()` anchor called from `GameWorld`.
- Bump `proto::kProtocolVersion` when a network message changes, and `bsp::kVersion` when the
  map format changes.
