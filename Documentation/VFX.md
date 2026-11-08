# Visual effects

## Particles (`Client/Source/TacticalFPS/{Public,Private}/Fx/TfpsParticles.*`)

`tfps::fx::System` is engine-independent and deterministic for a given seed
and call sequence. It holds up to 3,000 particles; anything beyond that is
dropped and counted. Each particle has a look, position, velocity, life,
size growth, drag, gravity (negative for rising smoke) and bounce.
Colliding particles move one axis at a time, so when one would end up
inside a map solid it bounces on that axis. On landing they get floor
friction and come to rest once slow.

| Effect | Particles |
|--------|-----------|
| Impact (concrete, brick, drywall, dirt) | dust puffs + colliding chips |
| Impact (metal, reinforced) | bouncing sparks + a little smoke |
| Impact (glass) | colliding shards |
| Impact (wood) | splinters + dust |
| Impact (player) | blood mist |
| Muzzle | a short flash + smoke puffs along the shot |
| Explosion (frag, breach) | fire, rising smoke, dust, bouncing debris and sparks, scaled by radius |
| Flashbang | bright burst + smoke |
| Breaking cell | chunks of its material from anywhere in the cell + dust |
| Smoke grenade | a cloud that grows to the gadget radius and stays filled until the grenade stops |

The smoke cloud keeps a set of evenly spread slots (a golden-angle spiral
through a dome over the grenade). Each slot gets a new puff before its
current one starts to fade, and puff lifetimes are staggered, so the cloud
has no holes to see through. Earlier versions left gaps that
depended on the seed: random placement left 4–15 % of the dome uncovered,
and slots that refreshed only after their puff died left up to 28 %.

## Tests (`Client/NetCoreTests/src/FxTests.cpp`)

* Impacts produce the right looks for each surface, flying out of the
  surface.
* Particles never pass through the floor or a wall, and some come to rest.
* Runs are deterministic; alpha and size stay in range; every effect ends.
* The capacity is a hard limit.
* A 4 m smoke cloud covers ≥ 95 % of its inner dome at 4, 8 and 12 s
  (measured 100 % on several seeds). After it stops, it clears completely.

## Unreal (`UTfpsFxSubsystem`, `ATfpsParticleFx`), mock-checked only

The subsystem owns the system. Effects are added from these sources:

| Effect | Source |
|--------|--------|
| Muzzle and impact effects | `ATfpsShotFx` |
| Explosions | `ATfpsShotFx` |
| Flashes and smoke clouds (which replace the grey placeholder sphere) | `ATfpsGadgetFx` |
| Breaking cells (which replace the earlier physics-cube debris pool) | `ATfpsArenaBuilder` |

`ATfpsParticleFx` advances the system every frame and draws one instanced
mesh per look. 🔒 The meshes are tinted engine spheres and cubes. The
placeholder material is opaque, so fading is shown by shrinking. Niagara
systems with authored textures replace this.

Limitations:
* Particles collide with the static map, not with destroyed or added cells.
* There are no decals yet; the tiny impact cubes in `ATfpsShotFx` stand in
  for bullet holes.
* Nothing has been seen in the engine yet.
