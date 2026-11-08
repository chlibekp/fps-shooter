## 3D game audio from the procedural synthesiser in the native core
## (Client/.../Audio/TfpsSynth: every sound is generated, no recordings).
## Sounds are rendered once per variation and cached as AudioStreamWAV.
class_name TfpsAudio
extends Node3D

const MAX_VOICES := 40

var net: TfpsNet
var master_volume := 0.8
var effects_volume := 1.0
var _cues: Array = []
var _shot: Array = []
var _step: Array = []
var _impact: Array = []
var _impact_player := 18
var _cache := {}
var _voices: Array[AudioStreamPlayer3D] = []
var _ui: Array[AudioStreamPlayer] = []
var _next := 0
var _cue_ids := {}


func setup(p_net: TfpsNet) -> void:
	net = p_net
	var s := net.request({"op": "sounds"})
	_cues = s.get("cues", [])
	_shot = s.get("shot", [])
	_step = s.get("step", [])
	_impact = s.get("impact", [])
	_impact_player = int(s.get("impact_player", 18))
	for i in _cues.size():
		_cue_ids[_cues[i]["name"]] = i
	if AudioServer.get_bus_index("Reverb") < 0:
		AudioServer.add_bus()
		var bus := AudioServer.bus_count - 1
		AudioServer.set_bus_name(bus, "Reverb")
		var rv := AudioEffectReverb.new()
		rv.room_size = 0.6
		rv.damping = 0.6
		rv.wet = 0.18
		AudioServer.add_bus_effect(bus, rv)
	for i in MAX_VOICES:
		var p := AudioStreamPlayer3D.new()
		p.bus = "Reverb"
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		p.unit_size = 4.0
		add_child(p)
		_voices.append(p)
	for i in 6:
		var u := AudioStreamPlayer.new()
		add_child(u)
		_ui.append(u)


func cue(name: String) -> int:
	return _cue_ids.get(name, -1)


func shot_cue(weapon_id: int) -> int:
	return int(_shot[weapon_id]) if weapon_id >= 0 and weapon_id < _shot.size() else 0


func step_cue(material: int) -> int:
	return int(_step[material]) if material >= 0 and material < _step.size() else int(_step[0]) if not _step.is_empty() else 9


func impact_cue(surface: int) -> int:
	if surface == 254:
		return _impact_player
	return int(_impact[surface]) if surface >= 0 and surface < _impact.size() else 14


func _stream(cue_id: int, variant: int) -> AudioStreamWAV:
	var info: Dictionary = _cues[cue_id]
	var v := variant % maxi(1, int(info["variants"]))
	var key := cue_id * 64 + v
	if _cache.has(key):
		return _cache[key]
	var pcm := Marshalls.base64_to_raw(net.request({"op": "sound", "cue": cue_id, "variant": v}).get("pcm", ""))
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = 48000
	w.stereo = false
	w.data = pcm
	if info["loop"]:
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_end = pcm.size() / 2
	_cache[key] = w
	return w


func play_at(cue_id: int, at: Vector3, volume := 1.0) -> void:
	if cue_id < 0 or cue_id >= _cues.size():
		return
	var info: Dictionary = _cues[cue_id]
	if not info["spatial"]:
		play_2d(cue_id, volume)
		return
	var p := _voices[_next]
	_next = (_next + 1) % _voices.size()
	p.stream = _stream(cue_id, randi())
	p.position = at
	p.max_distance = float(info["range"])
	p.volume_db = linear_to_db(maxf(volume * master_volume * effects_volume, 0.0001))
	p.pitch_scale = randf_range(0.96, 1.04)
	p.play()


func play_2d(cue_id: int, volume := 1.0) -> void:
	if cue_id < 0 or cue_id >= _cues.size():
		return
	for u in _ui:
		if not u.playing:
			u.stream = _stream(cue_id, randi())
			u.volume_db = linear_to_db(maxf(volume * master_volume, 0.0001))
			u.play()
			return
