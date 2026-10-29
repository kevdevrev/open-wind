# Open Wind

Open Wind owns the wind state, CPU sampling contract, wind effects, shelter
caches, and settings for a compatible Community Shaders fork. Open Shaders retains
the shaders, GPU wind evaluation, vegetation springs, mesh authoring, grass
contact rendering, and motion vectors.

This standalone repository targets SE, AE, and VR. The runtime and optional
bridges build independently of the Open Shaders source tree.

The focused Open Shaders extraction preserves upstream's existing wind controls
and deformation. The fine-wind/shelter rendering, Grass Meshes and Integrations
panels, and Shapes overlay described below belong to the full-feature reference
integration and remain separate integration work. This repository retains their
provider implementations; the focused renderer advertises only what it supports.

## Requirements

- The matching SKSE runtime and Address Library for Skyrim SE, AE, or VR.
- A CS fork implementing Open Wind's renderer interface. The matching
  Open Shaders extraction provides this bridge; an unmodified CS fork does not.

Install `OpenWind.dll` under `Data/SKSE/Plugins` alongside the compatible renderer.
Open Wind has no independent update loop: the renderer advances the provider
once per frame, then consumes that same state on the GPU. CPU sampling never
advances the simulation. No GPU shaders are packaged or built by this project.

Open Shaders remains usable without Open Wind. The renderer selects ordinary
vegetation wind through runtime flags, keeping the same shader-cache feature
state whether `OpenWind.dll` is present or absent. Wind simulation textures and
passes are skipped when no compatible provider is connected.

## Settings

All settings UI is renderer-owned. The full-feature Open Shaders Wind page has
Wind Field, Wind Effects, Trees, Tree Meshes, Grass Meshes, Grass, and Integrations.
SKSE Menu Framework is not required.
Mesh authoring retains the existing JSON files, including installed presets.
Save JSON and Revert Unsaved apply to mesh rules separately from global settings.

Open Wind supplies the Wind Effects panel definitions through its separate UI
API; Open Shaders draws them with native controls. Wind Field, Trees, Grass,
mesh authoring, and Integrations retain their specialized renderer menu code.
Scene Manager can apply
weather and location overrides to Wind Field, Trees, and Grass. Wind Effects,
including transient field coverage, remain global and cannot be scene overrides.
Open Wind owns the running wind state and effects; the renderer forwards
settings changes through the UI API without changing existing settings paths.

Wind Effects includes continuous directional wind for aimed concentration spells
such as Flames, with independent casting-hand sources and configurable strength
and release time under Projectile Magic. Dragon breath uses the Dragons controls
for strength, reach, cone angle, and release time. Both use the shared transient
field, including the physics wind channel, without adding native Havok impulses.
The ordinary spell and dragon wind enable switches still apply. Dragons also
produce configurable moving flight/crash wakes, wingbeat volumes with separate
start/end radii, and directed wind during soul absorption. Wind Field's Shapes
debug view draws the active volumes and wake diagnostics on flat-screen Skyrim.
Concentration wind follows the latest observed projectile direction and hand
origin; its field volume does not perform an additional wall trace.

Global defaults use Open Shaders' normal save/load configuration. At startup the
renderer supplies its saved Wind settings to Open Wind. This UI integration no longer
loads or writes `Data/SKSE/Plugins/OpenWind/Settings.json`; an existing file is
left untouched. Per-mesh authoring remains separate from Scene Manager overrides.

## Optional integrations

HDT-SMP and Dynamic Wind Framework support ships as separate optional SKSE DLLs:
`OpenWindHDT.dll` and `OpenWindDynamicWind.dll`. Install either bridge under
`Data/SKSE/Plugins` alongside `OpenWind.dll`. Each bridge samples public Open Wind
API and activates independently when its required host API is available:

- FSMP plugin interface 2.1 with wind-controller descriptor 2.
- Dynamic Wind provider API version 3.

The ordinary host releases must expose those APIs before they can use this
integration. Missing or unsupported hosts leave their bridges inactive. While
Open Wind has no ready field snapshot, both hosts retain native wind.
The host forks expose generic extension points and have no Open Wind or renderer
dependency. The Dynamic Wind bridge selects field components and supplies wind
samples; Dynamic Wind owns animation playback and physics application.

The full-feature renderer's Integrations tab shows bridge detection status,
with settings and enable toggles only for installed compatible bridge DLLs. HDT
retains velocity response, flutter amount/rate, torque, XML sensitivity, shelter,
and full transient forces. Dynamic Wind retains its object settings and drives
authored animation rates from local wind without an extra oscillator or rate
smoothing. Body-center samples preserve three-dimensional wind and exclude
native Havok impulses. Push Response Curve selects Linear or Cubic before sign
sensitivity is applied. Linear preserves weak wind; Cubic uses the original
capped strength curve, calibrated to match Linear at wind strength 0.5. Both
preserve XYZ direction and frame-time scaling. Native or unavailable-provider
physics retain Dynamic Wind's original cubic, horizontal impulses.
Model and base-object variants use base wind, gusts, and fine wind without
transients. Their selection strength is capped to the original host's 0–1 wind
range; the host retains its authored stages, thresholds, and direction weighting.
Banner stages update on initial tracking and wind-target changes, following the
original Dynamic Wind timing rather than continuously following passing gusts.

Dynamic Wind's status reports sampling calls and per-handler requested,
successful, and transient-hit sample counts since startup. A ready connection
alone does not confirm that Dynamic Wind is sampling configured objects.
Transient hits describe the sampled field before filtering; physics still
excludes native Havok impulses and model variants exclude all transients.

Edits apply immediately. Each installed bridge saves its own settings:

- HDT: `Data/SKSE/Plugins/OpenWind/Bridges/HDT.json`.
- Dynamic Wind: `Data/SKSE/Plugins/OpenWind/Bridges/DynamicWind.json`.

Save Integrations, Reload, Reset, and Retry Connections act on installed bridges.
These settings are global and separate from Scene Manager and the normal wind
settings save. The old combined `Integrations.json` and earlier connector settings
are not imported.

## Public sampling API

Consumers include `include/OpenWind/API.h`, resolve `GetOpenWindAPI` from the
already-loaded `OpenWind.dll`, and request `OpenWind::kAPIRevision`. Check the
structure sizes, required function pointers, and `IsReady()` before
sampling. A missing or incompatible provider should disable the consumer's wind
integration, rather than loading another copy of the DLL.

`SampleWind` accepts world-space positions and returns separate base, broad gust,
fine wind, and transient vectors. It also supplies combined totals and the
transient/total alternatives that exclude events already applying native Havok
force. Each successful batch observes one immutable snapshot and frame ID.

The sampling API returns `terrainExposure`, `obstacleExposure`, and `windExposure`
(0 sheltered, 1 exposed). The first two are diagnostic components; `windExposure`
is the combined attenuation already applied to base, gust, and fine wind vectors.
Do not multiply those vectors by exposure again. Transient vectors remain
unaffected. At each cached lattice node the stronger of terrain and object
shelter wins, then the result is interpolated. Consequently the combined value
can differ from the minimum of the two interpolated diagnostic values at edges.
The HDT bridge similarly combines this exposure with HDT's obstruction
factor using the stronger shelter, without multiplying overlapping shelter.

Wind vectors retain the existing normalized response scale. They are not metres
per second or a vegetation displacement. Consumers choose their own physical or
animation response and may weight the components independently. The native-force
exclusion contract prevents a physics consumer from applying a native impulse a
second time; visual consumers can use the complete transient contribution.

Open Wind's sampling, renderer, UI, and optional bridge interfaces report revision 1
as informational metadata. Revision numbers do not gate discovery, binding, or
frame updates. Required callbacks and buffer sizes are still checked. Revision
changes require explicit owner approval.

The renderer binds during SKSE post-post-load, signals data-loaded initialization,
and calls `Update` at its existing wind reset point once per frame. The returned
frame contains the current and previous field states, transient arrays, and
grass's separate fine-wind transport. Those values feed the existing GPU passes.
Wind settings use the UI API. Open Wind owns authoring command parsing, filtering,
and JSON responses; the renderer supplies typed callbacks to its mesh-rule
backend and retains all authoring UI. Sampling clients use only the public API
and do not participate in the renderer lifecycle.

The renderer advertises supported field components in `RendererHost::capabilities`.
A baseline renderer leaves this mask zero. Fine Wind, terrain shelter, and object
shelter are evaluated only when their respective capability is present, keeping
CPU samples and GPU vegetation on the same supported field. Saved Open Wind
settings remain intact; unsupported contributions are inactive for that renderer.
This does not select shader permutations or change interface revision numbers.

## UI API

`include/OpenWind/UIAPI.h` is independent of the sampling and renderer headers.
Resolve `GetOpenWindUIAPI` from the already-loaded `OpenWind.dll`. Its functions
reuse the existing settings and command handlers: `GetSettings`, `LoadSettings`,
`ImportSettings`, `RestoreDefaults`, `RunAction`, `Command`, and `GetControls`.
Call them on the renderer/menu thread. No ImGui objects, STL objects, or allocation
ownership cross the DLL boundary. A missing UI API does not disable valid wind
rendering or sampling.

`GetControls("windEffects", buffer, &size)` returns immutable, cached UTF-8 JSON.
A null buffer queries the required size, including the terminating null. The
document contains `tabs`, each with an `id`, localized `label`, and `controls`.
Open Wind owns all eight effect panels; OS no longer compiles their effect structs
or control lists. New effect controls using the supported schema can therefore
ship in Open Wind alone. The other menu implementations and typed renderer data
remain in the renderer; Open Wind has no ImGui dependency.

Control nodes use `checkbox`, `float`, `group`, or `text`. Labels and tooltips are
`{key, text}` pairs for translation keys and fallback text; value `path`s are JSON
pointers relative to the effect settings. Groups contain a `controls` array and
can be collapsible. Numeric controls supply `min`, `max`, `format`, and optional
`logarithmic`. `visibleWhen` and `disabledWhen` accept `{path, equals}`, compound
`all`/`any` conditions, or `{available: bool}` from the tab's availability action.
`minimumPath` and `onChangeClampMinimum` preserve dependent numeric bounds.
Wind Effects remain global and disabled during Scene Manager editing; visual
Wind Field, Trees, and Grass paths retain their existing Scene Manager behavior.
The `uiControls` devbench query exposes UI connection status and cached descriptors.

`UIAPI::Command` handles `treeRules`, `grassRules`,
`saveTreeRules`, `saveGrassRules`, `revertTreeRules`, `revertGrassRules`,
`setTreeRule`, `setTreeBendEnabled`, `addGrassRule`, `setGrassRule`, `setUniversalTreeResponse`, and
`resetUniversalTreeResponse`. Call it synchronously on the renderer/menu thread.
Its response callback receives borrowed UTF-8 JSON once; copy it during the
callback if needed afterward. There is no size-probe call. A false return means
the request or transport failed and does not guarantee that a mutation was
rolled back; do not automatically retry edits. The response's `success` field
reports the mesh operation's result. This is separate from the public wind
sampling API. Rule queries accept `search`, `offset`, and `limit` (default 100,
maximum 500), and return total/matching/unsaved counts. Tree entries include
`bendEnabled`. Universal response edits accept partial updates. A renderer may
leave unsupported authoring callbacks null; those commands report an explicit
unsupported-operation result without disabling wind rendering.

The same command transport exposes `integrations`, `setIntegrations`,
`saveIntegrations`, `reloadIntegrations`, `resetIntegrations`, and
`retryIntegrations`. Open Wind forwards these commands to installed compatible
bridge DLLs. Integration edits accept `hdt` and `dynamicWind` objects;
`setIntegrations` optionally accepts `save: true`. The query succeeds even when
bridges are absent. Each child reports `installed`, `status`, and `registered`;
only installed bridges include settings and their own `settingsPath`. There is
no combined settings path. Host registration changes are queued on the game
thread. A failed save leaves runtime edits applied.

## Terrain and object wind shelter

Wind Field contains separate Terrain Shelter and Object Collision controls.
Both save with the visual settings and support Scene Manager. Object coverage radius controls the area sampled around the player. Detailed
Terrain Radius controls local refinement; an installed worldspace heightmap
provides terrain shelter beyond that radius, including outside tree spring
coverage. Upwind distance controls how far
each sample searches for an obstruction. Increasing one does not increase the
other. Distances are Skyrim world units.

| Setting | Default | Range |
| --- | --- | --- |
| Enable Terrain Shelter | On | On/off |
| Shelter Strength | 0.85 | 0–1 |
| Detailed Terrain Radius | 16384 | 4096–32768 |
| Upwind Distance | 3072 | 128–16384 |
| Slope Softness | 0.25 | 0.05–1 |
| Enable Object Collision | On | On/off |
| Collision Strength | 1 | 0–1 |
| Object Coverage Radius | 4096 | 2048–60000 |
| Object Upwind Distance | 2048 | 256–4096 |

The terrain cache uses up to three 65 by 65 height grids. The near grid retains
256-unit spacing, the middle grid uses 1024-unit spacing, and the far grid expands
with coverage. The middle grid is omitted when the outer radius is at most
15872 units. A shared budget of 64 terrain queries per queued game-thread task
prioritizes nearby samples, with updates limited to ten per second and a reserved
share for the outer grids.
Overlapping samples survive movement and newly discovered shelter fades in.
Installed xLODGen/custom worldspace DDS heightmaps supply terrain heights to
both CPU and GPU consumers. Outside the local exposure grids, a directional
heightmap query provides shelter throughout the heightmap bounds. Inside them,
loaded landscape takes precedence and the heightmap fills missing heights.
The same fallback shelters LOD trees beyond the spring fields. Without a usable heightmap,
coverage is limited to terrain the game can supply. No Terrain Shadows feature
or GPU readback is required. The loader supports BC4/ATI1, R16 UNORM, R16 float,
and R32 float DDS files in `Data/textures/heightmaps` or worldspace folders under
`Data/textures/Terrain`, using the existing custom/xLODGen filename bounds.

Object collision uses cached horizontal Havok rays at six ground-relative
heights. It includes loaded static buildings, walls, and rock collision, excludes
terrain and dynamic bodies, and preserves openings below bridges. Each of three spatial cascades
stores two direction grids for blending during wind changes. Each grid has
32 by 32 columns: near spacing is at most 256 units out to 3968 units; middle
spacing is at most 1024 units out to 15872 units; the far cascade extends to the
selected maximum, up to 60000 units. Cascades outside the selected radius are
disabled. Matching GPU exposure cascades preserve this detail through rendering.

Ray queries run only in game-thread tasks that stop scheduling after 32 actual
Havok casts or 0.75 ms of query work, never inside a wind sampling callback. A
persistent weighted schedule prioritizes near over middle and far work. Unloaded
columns are skipped before querying terrain or casting rays. Results refresh
gradually; unknown inner samples fall back to known outer samples, and unknown
collision in every cascade leaves wind exposed. Extending coverage does not
stretch the near grid or load distant Havok collision.
Terrain and object radius controls remain independent even when object coverage
requires a larger shared ground cache. Animated-static collision is included,
but its five-second refresh target cannot track fast door motion immediately.

The GPU builds up to three 32 by 32 by 16 exposure cascades plus ground metadata.
Height layers are denser close to the ground so short obstacles are not averaged
across hundreds of vertical units. CPU consumers evaluate the same lattice
nodes and interpolation. Terrain and objects contribute the stronger shelter at
each node, rather than applying two successive reductions. Near coverage blends
into far coverage, and the outer boundary blends into worldspace terrain shelter
when the heightmap is available, otherwise open wind. GPU history retains
the preceding exposure volume and world shelter parameters for vegetation
motion vectors. The renderer uploads the immutable heightmap once per
worldspace through the renderer interface; Open Wind retains ownership
of its CPU data. Both DLLs must be updated together. The public sampling API
and integration contracts are unchanged.

Select **Terrain Shelter**, **Object Collision**, or **Combined Wind Shelter**
in **Wind Debug View** to inspect each contribution. Red means full exposure,
blue strong shelter, yellow partial shelter, magenta missing local cached data,
and gray outside coverage or disabled. Local coverage does not guarantee that
every upwind sample is cached. Camera readouts report all three exposure values.
The Terrain Shelter menu reports whether the worldspace heightmap has reached
the renderer. Wind diagnostics expose `worldTerrainHeightmapAvailable`,
`worldTerrainHeightmapUploadFailed`, `objectShelterCoverageRadius`, and
`objectShelterCascadeCount`.
Devbench exposes `setTerrainShelter`, `setObstacleShelter`, and
`setTerrainShelterDebug`; the debug command accepts `view` equal to `terrain`,
`objects`, or `combined`, plus an optional `enabled` boolean.

Trees use height-aware shelter for bend and detail motion. Beyond spring
coverage, trees sample the same traveling ambient gusts directly, using the
spring target's gust sensitivity and soft limit. Current and previous samples
follow their respective wind histories. This adds no distant transient response
and leaves the existing spring selection boundaries unchanged. Grass applies
ground-level shelter before ambient forcing joins transient forcing in its
spring. Existing wave phases and authored vertex weights remain unchanged.
Grass on elevated structures still uses the terrain beneath it. Wind effects
remain unaffected, including the distinction between visual and native Havok
transient impulses.

This models wind weakening behind obstructions, not fluid flow around them.
Static meshes need authored collision and loaded physics data; thin openings
smaller than the sampling grid may be lost. The expanded implementation has not
yet been verified in game; appearance and cost need runtime testing.

## Building

Use Visual Studio 2022 with C++23 support, CMake 3.25 or later, and vcpkg. Set
`VCPKG_ROOT` to the vcpkg checkout. When a build is wanted, run from this directory:

```powershell
cmake --preset Release
cmake --build --preset Release
cmake --install build/release --config Release
```

The Release build preset builds the core and both optional bridge DLLs. The
`Core` configure/build preset disables both bridge targets and uses its own
`build/core` directory. Each bridge can also be selected independently with
`OPENWIND_BUILD_HDT_BRIDGE` and `OPENWIND_BUILD_DYNAMIC_WIND_BRIDGE`.
The default install destination is `build/release/package`, laid out as a Skyrim
Data directory. CPack creates separate component ZIPs after building:

```powershell
cpack --config build/release/CPackConfig.cmake -C Release
```

| Component | DLL | Install choice |
| --- | --- | --- |
| Runtime | `OpenWind.dll` | Core wind runtime |
| HDTBridge | `OpenWindHDT.dll` | Optional HDT-SMP bridge |
| DynamicWindBridge | `OpenWindDynamicWind.dll` | Optional Dynamic Wind Framework bridge |

Each bridge archive includes its applicable license notices. Installing the core
archive alone leaves both integrations absent; install either bridge separately
with its compatible host.

The project fetches its pinned CommonLibVR dependency. To use an explicit
existing dependency checkout, set `-DOPENWIND_COMMONLIB_SOURCE=<absolute-path>`.
No sibling repository is detected implicitly. Open Shaders private headers,
libraries, build directories, and shader compilers are not required.

The sampling header is standalone C++; renderer bridge headers additionally use
DirectXTK types. Public renderer data is scoped to the `OpenWind` namespace
so it can coexist with the renderer's own wind structs. Runtime classes and
wind-effect settings implementations are private under `src/Runtime/`. To stage
the complete SDK separately:

```powershell
cmake --install build/release --config Release --component SDK --prefix build/sdk
```

The existing CPU wind regression tests are retained under `tests/`. They are
excluded by default. `OPENWIND_BUILD_TESTS=ON` enables the CPU test target and
its Catch2 dependency; `ctest --test-dir build/release -C Release` runs it after
building. The numerical conformance fixture is data only; this project does
not compile or run shader tests.

## Consuming the SDK with CMake

Use `FetchContent_Declare(OpenWind ...)` with this repository, a pinned commit
hash in `GIT_TAG`, `GIT_SUBMODULES ""`, and `SOURCE_SUBDIR api`. After
`FetchContent_MakeAvailable(OpenWind)`, link your consumer target privately to
`OpenWind::SDK`.

The SDK target supplies only public headers and the C++20 language requirement.
It does not configure the root project, build any DLL, or fetch the runtime's
dependencies. Consumers using renderer headers also supply DirectXTK, as before;
the sampling and UI contracts do not require it. Runtime discovery remains through
the exported interfaces in `OpenWind.dll`.

For local SDK development, set `FETCHCONTENT_SOURCE_DIR_OPENWIND` to the absolute
path of an Open Wind checkout. This bypasses the Git download. Remote consumers
need repository access; public CI cannot fetch this repository while it is private.
The public SDK headers use LGPL-3.0-or-later; see `COPYING.LESSER`, `COPYING`
and `NOTICE.md`. The implementation retains the default GPL license.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/Runtime/` | Wind state, CPU sampling, effects, shelter, settings and commands |
| `src/Integrations/` | Runtime bridge discovery and the two optional bridge DLLs |
| `include/OpenWind/` | Public sampling, renderer, UI and bridge interfaces |
| `api/` | Headers-only CMake SDK target |
| `extern/` | Generic host API headers and their provenance/license notices |
| `tests/` | Existing CPU wind regression tests and numerical fixture |

Open Wind's public sampling API, renderer frame API, UI API, and optional
bridge settings API remain separate contracts. Renderers consume only public
headers. Host mods consume their own generic extension contracts, and bridge
DLLs connect those hosts to Open Wind's sampling API.

## License

### Default

Open Wind contains wind code extracted from Open Shaders. Its default license is
[GPL-3.0-or-later](COPYING), with the Modding Exception and GPL-3.0 Linking Exception
(with Corresponding Source) in [EXCEPTIONS.md](EXCEPTIONS.md). The Modded Code is
Skyrim and its variants; the Modding Libraries are SKSE and CommonLib and its
variants. Third-party files retain their own license notices.

### Public API headers

All `.h` files under `include/OpenWind/` use [LGPL-3.0-or-later](COPYING.LESSER),
including the public sampling, renderer, UI and bridge contracts and their
supporting data types. This separate license does not apply to the implementation
under `src/`. See [NOTICE.md](NOTICE.md) for the scope and attribution.
