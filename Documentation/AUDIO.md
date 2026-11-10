# Audio

Every sound in the game is synthesised by our own code. No recordings or
third-party sound libraries are used, so all audio is original.

## Synthesis (`Client/Source/TacticalFPS/{Public,Private}/Audio/TfpsSynth.*`)

The synthesiser is engine-independent C++17 and deterministic:
`tfps::audio::Render(cue, variant)` always returns the same 48 kHz mono
16-bit samples. Recipes combine these building blocks:

* seeded noise through RBJ biquad filters (low-pass, high-pass, band-pass);
* sine tones with exponential pitch glides;
* decaying modal partials for struck metal, glass and wood;
* attack and decay envelopes;
* `tanh` soft clipping;
* a small Schroeder reverb (four combs, two all-passes) for tails.

| Group | Cues |
|-------|------|
| Weapons | rifle, SMG, marksman rifle, pistol and shotgun shots (3–4 variations each), dry fire, magazine out / in, bolt |
| Movement | footsteps on concrete, wood, metal and dirt (6 variations each), landing |
| Impacts | concrete, wood, metal, glass, flesh |
| Destruction and gadgets | glass / wood / wall breaking, explosion, flashbang, tinnitus, smoke hiss, grenade bounce, drone motor (seamless loop), device beep, reinforcement, barricade |
| Feedback | hit marker, kill confirmation, round start / win / loss, menu move / accept |

Each cue carries:
* a peak level, so cues have consistent relative loudness;
* an audible range in metres;
* whether it is positional;
* its number of variations.

## Tests (`Client/NetCoreTests/src/AudioTests.cpp`)

* Every cue renders, is 0.03–3 s long and is normalised to its peak
  without clipping. Every one-shot ends in silence (no click). Rendering is
  deterministic.
* Variations differ from each other, and the variant number wraps.
* The drone loop is seamless: the jump from its last sample back to its
  first is no bigger than any step inside it.
* Character checks on the share of energy below 400 Hz:
  * explosions and landings are bass-heavy (> 50 %);
  * glass breaking and menu ticks are bright (< 5 %);
  * the marksman rifle is heavier than the pistol.
* The WAV encoder writes a correct header. Weapons and materials map to the
  right cues.

`tfps_render_sounds <dir>` writes all 98 variations as WAV files, so they
can be listened to. `Scripts/test_all.sh`
renders them to `build/sounds`. The recipes were also tuned by checking
waveforms and spectrograms. For example, the first explosion recipe was a
sustained low sine and is now noise-driven, and metal footsteps were damped.

## Playback in the Godot client (`Game/scripts/audio.gd`)

* **Source:** the synthesiser runs inside the client's native extension. Each
  cue and variation is rendered on first use, sent to GDScript as 16-bit PCM
  (`sound` request) and cached as an `AudioStreamWAV`.
* **Playback:**
  * world sounds play on a pool of 40 `AudioStreamPlayer3D` voices, with
    inverse-distance falloff to the cue's range and a slight random pitch;
  * own-player and interface sounds play in 2D.
* **Reverb:** a reverb bus adds room tone.

Where the sounds are triggered (`Game/scripts/game.gd`):

| Sound | Trigger |
|-------|---------|
| Gunshots and impacts | shots from the server |
| Explosions, flashbang and tinnitus | events |
| Breaking | destroyed cells (one per material per frame) |
| Barricades and reinforcement | new and reinforced solids |
| Footsteps | stride-based, by the floor material under the feet: sprint loud, crouch / prone / walk quiet |
| Landing and reloads | the local predicted state |
| Drone motor | a looping 3D player on each drone |
| Hit and kill confirmation, round start / win / loss | events and match state |

## Limitations

* The development machine has no audio device, so the client's playback
  has not been listened to yet; the synthesised sounds themselves were
  reviewed from waveforms and spectrograms.
* There is no occlusion, and no reverb zones by room. Sounds through walls
  are only quieter with distance.
* Footstep surfaces use the static map (a destroyed floor still sounds like
  its material).
* There is no music.
