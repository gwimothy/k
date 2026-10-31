# TrenchBroom setup

1. Install [TrenchBroom](https://trenchbroom.github.io/) (2024.1 or newer).
2. Copy this folder into TrenchBroom's `games/` directory as `k/`:
   - Windows: `%AppData%\TrenchBroom\games\k\`
   - macOS: `~/Library/Application Support/TrenchBroom/games/k/`
   - Linux: `~/.TrenchBroom/games/k/`
3. In TrenchBroom: Preferences → k → set the game path to this repository's root
   (the folder containing `base/`).
4. Create maps in Valve format and save them to `base/maps/src/`. The build compiles every
   `.map` there into `build/bin/base/maps/<name>.kbsp`, or run the compiler yourself:

   ```sh
   ./build/bin/kbsp -game base -o build/bin/base/maps/mymap.kbsp base/maps/src/mymap.map
   ```

5. In the engine: `map mymap`.

Rules of thumb (same as Source):
- The map must be sealed: no gaps to the void. The compiler prints a leak path if it isn't.
- Use `tools/toolsskybox` on brushes where the sky should show.
- Turn small or detailed brushes into `func_detail` to keep the BSP and vis fast.
- 1 unit ≈ 1 inch. The player is 72 units tall; doors are usually 128 high.
