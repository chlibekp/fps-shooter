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
| Pivot | Bottom centre of the asset |
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

| Asset | Size (m) | Tris LOD0 / LOD1 | Collision | Textures | Matches map solid |
|-------|----------|------------------|-----------|----------|-------------------|
| WoodCrate | 1.25 × 1.25 × 1.2 | 6,696 / 2,342 | 1 box | 1024² | `crate` |
| JerseyBarrier | 0.6 × 3.0 × 0.93 | 436 / 218 | 1 convex hull | 512² | `barrier` |
| ShippingContainer | 6.06 × 2.53 × 2.6 | 4,420 / 1,326 | 5 boxes | 512² | `container` |

Previews: `Assets/Exports/*/preview.png`.

## Importing into Unreal (manual step until an editor commandlet exists)

1. Drag `SM_<Asset>.fbx` into `Content/Environment/Props/<Asset>/`. Leave
   *Combine Meshes* off; collision is picked up from the `UCX_` meshes.
2. Open the mesh → LOD Settings → *Import LOD Level 1* → `SM_<Asset>_LOD1.fbx`.
3. Import the three textures. Set `_N` compression to **Normalmap**. Set
   `_ORM` to **Masks** (sRGB off).
4. Create `MI_<Asset>` from the project master material (Phase 5; until then
   the engine's default lit material with the three maps wired works).
5. Enable Nanite on the static mesh where the platform supports it (see
   `PLATFORMS.md`). The LOD1 FBX is used where Nanite is unavailable.

Planned: an Unreal Python/commandlet import script
(`Tools/AssetPipeline/unreal_import.py`) to make steps 1–4 automatic.

## Known limitations

* The texture bakes are procedural (noise/wave/voronoi). They look plausible
  but are not photo-sourced. Higher-fidelity materials are Phase 5 work.
* Wood grain on the crate's top face is generated in object space, so it
  does not follow each plank perfectly.
* Small parts such as the container corner castings get very little UV
  space. Some of them bake dark.
* The in-game graybox still uses engine cubes (`FTfpsPlaceholderAssets`).
  Placing these props over the matching map solids is part of the Phase 5
  map dressing.
