## Renders the first-person view model from a synthetic player state, for
## deterministic visual review of weapon poses and animations.
## godot --path Game -s res://tools/viewmodel_preview.gd -- OUT.png WEAPON_ID [key=value ...]
## Keys: team (0/1), reload (progress 0..1, empty reloads with empty=1),
## aim (0..1), inspect (seconds into the inspection), frames (default 60).
extends SceneTree

var _player: TfpsPlayer
var _frames := 0
var _wait := 60
var _out := "viewmodel.png"
var _state := {}


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_out = args[0] if args.size() > 0 else _out
	var weapon := int(args[1]) if args.size() > 1 else 1
	var opts := {}
	for a in args.slice(2):
		var kv := (a as String).split("=")
		if kv.size() == 2:
			opts[kv[0]] = kv[1]
	_wait = int(opts.get("frames", "60"))
	TfpsControls.install()
	var root3d := Node3D.new()
	get_root().add_child(root3d)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.6, 0.66)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.62, 0.64, 0.68)
	e.ambient_light_energy = 0.7
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 150, 0)
	sun.light_energy = 1.5
	root3d.add_child(sun)
	_player = TfpsPlayer.new()
	_player.input_enabled = false
	root3d.add_child(_player)
	var reload := float(opts.get("reload", "-1"))
	var w := {"id": weapon, "active": 1 if TfpsModels.weapon_class(weapon) == "pistol" else 0,
		"reloading": reload >= 0.0, "reload_progress": maxf(reload, 0.0), "reload_empty": opts.get("empty", "0") == "1",
		"ammo": 0 if opts.get("empty", "0") == "1" else 20, "shot_counter": 0, "equip_timer": 0.0}
	_state = {"eye": [0, 1.6, 0], "weapon": w, "side": int(opts.get("team", "0")), "alive": true,
		"aim": float(opts.get("aim", "0")), "velocity": [0, 0, 0], "on_ground": true}
	if opts.has("inspect"):
		_player.inspect()
		_player.set("_inspect_t", TfpsPlayer.INSPECT_TIME - float(opts["inspect"]))


func _process(delta: float) -> bool:
	_frames += 1
	var d := 1.0 / 60.0
	if _player.get("_inspect_t") > 0.0:
		_player.set("_inspect_t", _player.get("_inspect_t") + d)   # hold the inspection pose
	_player.apply(_state, d, 0.0)
	if _frames == _wait:
		get_root().get_texture().get_image().save_png(_out)
		print("saved ", _out)
		return true
	return false
