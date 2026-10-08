# RTS project conventions

- Target Windows 10/11 x64 only; C++20, MSVC, CMake. Project name is RTS.
- Keep simulation in logical map coordinates, independent of render pixels and camera.
- Measure movement, collisions, formations, combat and vision with the shared ground-plane metric in Types.hpp. It matches the screen plane up to a fixed scale; equal visible distances take equal time. Cell coordinates still author diamonds and static footprints. Never normalize a movement vector with the raw grid Euclidean norm or camera zoom.
- Terrain uses logical square cells projected as 2:1 diamonds through WorldView. Normal authored maps use Map::rectangular with dimensions divisible by 32; the projected outer boundary stays rectangular and clipped cells are never playable, including for air. Keep picking, placement, camera and minimap consistent with this geometry. Map(width,height) is the uncropped logical-grid constructor for diagnostic fixtures.
- Keep walkability rules in `Map::canStep`; test changes involving ramps, corners and height transitions.
- Water depth is a surface type independent of elevation. Thread unit movement types through navigation, interactions, production and rally checks; air has separate dynamic occupancy.
- Shallow and deep water share a flat elevation of -1 in the demo. Coastal ramps are dry lower land tiles pointing uphill; crossing a shore cliff without a ramp is forbidden for ground movement.
- Author map ramps at least two cells wide (normally 2–3); wide ramps allow lateral movement between matching adjacent lanes. Keep isolated one-cell ramps available for navigation tests.
- Group orders form compact circular crowds in continuous map coordinates, with spacing derived from collision radii, never cell-centre slots. Lower formation priorities prefer the front; arrival may relax personal destinations to avoid circling settled friends. Keep matching independent of selection order; IDs only break geometric ties.
- Unit collision remains solid for allies and enemies, including moving neighbours. Never push, teleport or phase units through a closed surround. Buildings and blocking environment footprints stay square.
- Resources are crystals: workers harvest finite nodes and carry cargo to the town hall before crediting the balance.
- Explicit gathering retains the clicked deposit as its origin across traffic retries and deliveries. Fallback nodes must stay near that origin, never be chosen globally by distance from the worker.
- Resolve files through `Paths`. Assets are relative to the executable, never the working directory or source checkout.
- Use Unicode Windows APIs and `std::filesystem::path`; do not hardcode machine-specific paths.
- User data belongs under the Windows LocalAppData known folder, separate from installed assets.
- Preserve source URLs, author credits, licenses and checksums when adding third-party assets.
- Build/test with `scripts/build.ps1 -Configuration Debug -Test`; use Release for distributable packages.
- Build output and temporary downloads belong under `out/`. Do not commit these or IDE state.
- Keep platform entrypoints limited to startup/CLI. Gameplay controllers live in src/game; Forge controllers in src/forge. Split rendering by responsibility, sharing RenderSupport rather than duplicating graphics helpers.
- Add regression tests to the relevant tests/*Tests.cpp suite; CoreTests.cpp is only the runner. Performance.cpp is an opt-in benchmark, not a timing gate in CTest. Use Release and report workload-specific timings, not inferred FPS.
- Fog visibility is a union of observers. Presentation caches must refresh on fog, terrain and map changes; dynamic minimap markers stay outside the cached raster.
- Keep the prototype scope explicit in README; do not claim crowd navigation, combat or saved games before implementing them.

- Environment objects have stable IDs, health/interactions and partial collision footprints independent of their visual layer.
- Environment activity derives from health: zero removes rendering/collision/vision, positive health restores the same instance and ID. Never erase destroyed environment records.
- Ground vision stops at trees and rocks but reveals the first blocker; air ignores vision obstacles. Keep movement occupancy separate from sight blockers.
- Fog feathering is presentation only. Entity visibility, selection and targeting must use logical FogOfWar, never the smoothed mask.
- World documents use the current JSON .rtsmap format, with no legacy migrations. Save authored data, never mutable match state; test play owns a separate Scenario copy.
- Visages Forge is a separate executable and Win32 frontend. Link WorldEditor and ForgeRenderer only into Forge, never the game. Share map formats, catalogs and rendering through libraries.
- Forge F9 launches the adjacent game executable with a temporary authored map and --forge-test; F10 exits that game process. Preserve the Forge document and undo history across play tests.
- Keep terrain/base material, free texture paint, and placed objects separate. Paint and fractional-position cosmetic decorations never alter navigation or sight.
- World assets come from assets/world/catalog.json; extend data catalogs instead of adding per-asset switches. Paths must resolve relative to executable assets.
- Editor undo is per stroke. Rebuild derived occupancy/vision after object edits; serialize IDs and transforms, not render caches.
- The test map has exactly one player slot and selectable team color. Select commanders from a data catalog; race is a commander attribute, not a hardcoded gameplay switch.
- Army supply has a hard maximum of 100; keep resource balance separate.
- Day/night is a simulation clock with phase-change events, independent of lighting.
- Keep commander meta-progression separate from in-match hero progression. Future abilities, passives, auras and items use shared effect systems; see docs/GAME_SYSTEMS.md.

