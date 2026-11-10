# Asset pipeline (Blender)

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
| Units | 1 Blender unit = 1 m |
| Axes | Z up; FBX export forward −Z, up Y |
| Pivot | Bottom centre of the asset (the build fails otherwise) |
| Naming | `SM_<Asset>` mesh, `SM_<Asset>_LOD1`, `UCX_SM_<Asset>_NN` collision, `T_<Asset>_BC/N/ORM` textures |
| Normal maps | Tangent space, **OpenGL** convention (green up), as Godot expects |
| ORM | R = ambient occlusion, G = roughness, B = metallic (import as linear / "Masks") |
| Base colour | sRGB |
| Collision | Simple convex `UCX_` primitives exported with LOD0 |
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
| WeaponCarbine | 0.048 × 1.027 × 0.338 | 2,388 / 1,074 | 0 | 512² | AR-7 Carbine (weapon 1) |
| WeaponSMG | 0.053 × 0.625 × 0.357 | 1,964 / 882 | 0 | 512² | K-9 SMG (2) |
| WeaponMarksman | 0.052 × 1.334 × 0.294 | 2,540 / 1,142 | 0 | 512² | M-308 Marksman (3) |
| WeaponPistol | 0.032 × 0.217 × 0.165 | 1,048 / 470 | 0 | 512² | P-9 Pistol (4) |
| WeaponShotgun | 0.044 × 1.075 × 0.189 | 1,196 / 538 | 0 | 512² | SG-12 shotgun (5) |
| Operator{A,B}{Torso, Head, Thigh, Shin, UpperArm, Forearm} | body parts | 3,456 · 4,008 · 512 · 1,076 · 484 · 860 (LOD1 half) | 0 | 512² | other players, per team |

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

## Weapons

`build_weapons.py` builds the five weapons from bevelled extruded
silhouettes and cylinders. Optics are open tubes, so the eye looks through
them when aiming. Each weapon is modelled along +X, then turned so the
barrel points along Blender +Y (Godot -Z). The manifest adds three points
in the exported model's space (metres, Blender axes):

| Field | Point |
|-------|-------|
| `grip_m` | top of the pistol grip: the game holds the weapon there |
| `muzzle_m` | barrel end: muzzle flashes and tracers start there |
| `sight_m` | optic centre or the line over the iron sights: aiming puts it on the view axis |

Godot maps a Blender point (x, y, z) to (x, z, -y).

Build one weapon only: `TFPS_WEAPON=WeaponPistol python3 Tools/AssetPipeline/build_assets.py weapons`.

## Operator

`build_operator.py` builds the operator as six body parts per team: A
(attackers: olive uniform, coyote gear, orange patches) and B (defenders:
slate uniform, black gear, blue patches). The parts are the torso with
plate carrier, the head with helmet, goggles and ear protection, and the
thigh, shin with boot, upper arm and forearm with glove. Each part is
modelled around its joint: hip, neck, knee, shoulder or elbow. Limbs hang
along -Z and are built for the right side; the game mirrors them. The
manifest records the joint as `joint_m`. `TfpsOperator` turns the parts at
the joints and solves the arms with two-bone IK onto the weapon. Without
the parts, it falls back to primitives.

Preview: `Scripts/godot/godot.sh --path Game -s res://tools/operator_preview.gd -- operators.png 160`.

## Using the assets in the game

The game client is Godot 4.7, which imports FBX directly.
`Scripts/godot/sync_assets.sh` copies each `SM_<Asset>.fbx`, its textures
and `manifest.json` into `Game/assets/` (generated, not in git).
`export_game.sh` runs it before exporting. Where the assets are missing, the
game falls back to procedural shapes.

* `TfpsProps` (`Game/scripts/props.gd`) dresses map solids whose tag and
  size match a prop: `crate` → WoodCrate, `barrier` → JerseyBarrier,
  `container` → ShippingContainer, `door` → WoodDoor, and window panes get a
  WindowFrame. A destructible solid shows the prop until its first cell
  breaks. Then the surviving cells replace it, so the visuals never disagree
  with gameplay.
* Gadgets are drawn with FragGrenade, BreachCharge, ReconDrone and
  WallCamera.
* `TfpsModels.weapon` (`Game/scripts/models.gd`) uses the weapon models for
  the first-person view model and for other players. It places them by
  `grip_m`, `muzzle_m` and `sight_m`.

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
