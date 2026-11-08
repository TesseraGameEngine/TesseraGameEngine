# Tools

Scripts and engine command-line tools for building content. Python scripts need Python 3.10+
(`fetch_assets.py` also needs Pillow: `pip install pillow`).

## Headland: the open-world showcase

Headland is a ~1.5 km coastal island streamed in 128 m sectors, built from CC0 scanned assets.
It shows hardware ray tracing (soft shadows, reflections), disk streaming, the physical sky,
volumetric clouds, the ocean, and real-scale models with levels of detail.

Build it in three steps from the repository root:

```powershell
# 1. Download the CC0 models and textures from Poly Haven (~1.2 GB, git-ignored)
python tools/fetch_assets.py

# 2. Turn the scanned models (up to millions of triangles) into real-scale LOD chains
build/Release/Tessera.exe --optimize-models=assets/external/polyhaven/models

# 3. Generate the world (terrain, materials, scatter, camp, streaming manifest)
build/Release/Tessera.exe --build-world=projects/Headland/scenes --world-preview=island.png
```

Play it:

```powershell
build/Release/Tessera.exe --game --scene=projects/Headland/scenes/headland.world
build/Release/Tessera.exe --game --scene=projects/Headland/scenes/headland.world "--start-at=View: Overview"
```

Viewpoints for `--start-at`: `View: Camp`, `View: Pier`, `View: Forest`, `View: Hilltop`,
`View: Cliffs`, `View: Overview`. Add `--no-raytracing` to compare against the rasterised fallback.

| File | What it does |
| :--- | :--- |
| `fetch_assets.py` + `assets_manifest.json` | Resumable, MD5-checked download of the curated asset list; merges Poly Haven's separate leaf masks into RGBA cut-out textures; writes `CREDITS.md` |
| `Tessera --optimize-model(s)` | `src/Tools/ModelOptimizer`: splits variant sets (a row of trees) into separate models, QEM-decimates solid parts, thins alpha-tested leaf cards, writes real-scale `_lod0..2.gltf` files |
| `Tessera --build-world` | `src/Tools/WorldBuilder`: the island height function, splat painting, Poisson scatter, fishing camp, viewpoints, base scene, sectors and `headland.world` |
| `make_rt_test_scene.py` | A small scene for comparing ray-traced and rasterised shadows / reflections |
| `make_stream_test_world.py` | A synthetic 8 x 8 sector world with a fly-over camera for streaming tests |
| `make_asset_gallery.py` | The optimized models side by side (LODs, materials, scale) |
