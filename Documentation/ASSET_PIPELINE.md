# Asset pipeline (Blender → Unreal)

Assets are built **procedurally** by Python scripts that run in headless
Blender. The scripts are the source of truth. Re-running them reproduces the
`.blend` files, baked textures and FBX exports exactly. This keeps assets
reviewable in git, and changes such as texel density, LOD ratios or
collision can be made in one place.

```bash
python3 Tools/AssetPipeline/build_assets.py            # all assets
python3 Tools/AssetPipeline/build_assets.py crate      # one asset
TFPS_TEX_SIZE=2048 python3 Tools/AssetPipeline/build_assets.py   # higher-res bake
```

Requires Blender 4.x (tested: 4.0.2 headless on Linux; on Debian/Ubuntu the
distro build also needs `python3-numpy` for the FBX exporter).

## Conventions

| Rule | Value |
|------|-------|
| Units | 1 Blender unit = 1 m (Unreal imports FBX meters as 100 uu) |
| Axes | Z up; FBX export forward −Z, up Y (Unreal default import) |
| Pivot | Bottom centre of the asset (the build fails otherwise) |
| Naming | `SM_<Asset>` mesh, `SM_<Asset>_LOD1`, `UCX_SM_<Asset>_NN` collision, `T_<Asset>_BC/N/ORM` textures |
| Normal maps | Tangent space, **DirectX** convention (green flipped at bake time) |
| ORM | R = ambient occlusion, G = roughness, B = metallic (import as linear / "Masks") |
| Base colour | sRGB |
| Collision | Simple convex `UCX_` primitives exported with LOD0 (auto-detected by Unreal) |
| LODs | LOD1 exported as a separate FBX, imported into the LOD1 slot |

## Outputs

```
Assets/Blender/<Asset>.blend
Assets/Exports/<Asset>/SM_<Asset>.fbx          LOD0 + UCX collision
Assets/Exports/<Asset>/SM_<Asset>_LOD1.fbx
Assets/Exports/<Asset>/T_<Asset>_BC.png
Assets/Exports/<Asset>/T_<Asset>_N.png
Assets/Exports/<Asset>/T_<Asset>_ORM.png
Assets/Exports/<Asset>/preview.png            Cycles look-dev render
Assets/Exports/<Asset>/manifest.json          dimensions, triangle counts, files
```

## Current assets

| Asset | Size (m) | Tris LOD0 / LOD1 | Collision | Textures | Used for |
|-------|----------|------------------|-----------|----------|----------|
| WoodCrate | 1.248 × 1.248 × 1.202 | 6,696 / 2,342 | 1 | 1024² | map solids tagged `crate` |
| JerseyBarrier | 0.6 × 3 × 0.93 | 436 / 218 | 1 | 512² | `barrier` |
| ShippingContainer | 6.061 × 2.532 × 2.6 | 4,420 / 1,326 | 5 | 512² | `container` (kit reference) |
| WoodDoor | 1 × 0.116 × 2.15 | 1,636 / 490 | 1 | 1024² | `door` (mapkit door slab) |
| WindowFrame | 1.5 × 0.35 × 1.15 | 756 / 378 | 1 | 512² | frames `window_glass` panes |
| BrickWallPanel | 2.4 × 0.3 × 3 | 108 / 54 | 1 | 1024² | one mapkit wall panel |
| ReconDrone | 0.28 × 0.146 × 0.205 | 1,320 / 528 | 1 | 512² | gadget 5 |
| BreachCharge | 0.24 × 0.08 × 0.34 | 564 / 224 | 1 | 512² | gadget 4 |
| FragGrenade | 0.0678 × 0.0745 × 0.1024 | 2,216 / 664 | 1 | 512² | gadget 1 |
| WallCamera | 0.1 × 0.1266 × 0.1189 | 880 / 308 | 1 | 512² | gadget 6 |

Previews: `Assets/Exports/*/preview.png`.

## Multi-surface assets (gadgets, hardware)

Props made of several surfaces (plastic, rubber, steel, paint) use
`tc.paint(part, rgb, metallic, roughness)` on each part before joining, and
`tc.painted_material()`. The values are stored in colour attributes. One
material renders them with subtle procedural wear, and the bake produces
one texture set:
* metallic is baked per texel into ORM.B;
* base colour is baked with metallic forced to 0, so metals keep their
  tint instead of baking black.

## Importing into Unreal

`Tools/AssetPipeline/unreal_import.py`, run in the editor (*Execute Python
Script*) or with `UnrealEditor-Cmd ... -run=pythonscript -script=...`,
imports every `Assets/Exports/*/manifest.json`:

* `SM_<Asset>` into `/Game/Environment/Props/<Asset>`, with UCX collision
  and LOD1;
* the textures with the right compression (N = normal map, ORM = masks,
  sRGB off);
* `MI_<Asset>` as a child of `/Game/Materials/M_TfpsMaster` if that parent
  exists, otherwise a small standalone material wiring the maps.

**Status: written against the UE 5 Python API, not yet run in an editor.**

Manual equivalent:
1. Drag `SM_<Asset>.fbx` into `Content/Environment/Props/<Asset>/` with
   *Combine Meshes* off.
2. Import `SM_<Asset>_LOD1.fbx` into LOD 1.
3. Import the textures (N: Normalmap; ORM: Masks).
4. Create the material.

## Where the game uses imported assets

The game looks for `/Game/Environment/Props/<Asset>/SM_<Asset>` at runtime
(`FTfpsPlaceholderAssets::ImportedProp`) and falls back to placeholder
shapes while an asset is not imported:

* `ATfpsArenaBuilder` dresses map solids whose tag and size match a prop:
  `crate` → WoodCrate, `barrier` → JerseyBarrier, `door` → WoodDoor, and
  `window_glass` (1.4 × 1.1 m) → WindowFrame around the pane. A
  destructible solid shows the prop until its first cell is destroyed. The
  prop is then replaced by the surviving cells, so the visuals never
  disagree with gameplay.
* `ATfpsGadgetFx` draws grenades, charges, drones and cameras with
  FragGrenade, BreachCharge, ReconDrone and WallCamera.

## Known limitations

* The texture bakes are procedural (noise/wave/voronoi/brick). They look
  plausible but are not photo-sourced.
* Placed devices (charges, cameras) are drawn without the surface
  orientation (the server does not replicate the wall normal yet).
* Wood grain on the crate's top face is generated in object space, so it
  does not follow each plank perfectly.
* Small parts such as the container corner castings get very little UV
  space. Some of them bake dark.
* Walls and floors are still drawn as tinted cell cubes. The
  BrickWallPanel material is not applied to them yet: it needs world-aligned
  UVs on the cell instances.
