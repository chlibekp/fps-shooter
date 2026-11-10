## Renders operators (both teams, several poses) to a PNG for visual review.
## godot --path Game -s res://tools/operator_preview.gd -- OUT.png [facing_deg]
extends SceneTree

var _ops: Array = []
var _frames := 0
var _out := "operators.png"
var _yaw := 160.0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	if args.size() > 1:
		_yaw = float(args[1])
	var root3d := Node3D.new()
	get_root().add_child(root3d)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.6, 0.66)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.62, 0.66)
	e.ambient_light_energy = 0.6
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.light_energy = 1.4
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(20, 20)
	ground.mesh = pm
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.4, 0.4, 0.38)
	ground.material_override = gm
	root3d.add_child(ground)
	var cam := Camera3D.new()
	cam.fov = 50
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(0.2, 1.4, 4.2), Vector3(0, 0.95, 0))
	var poses := [
		{"x": -1.6, "team": 0, "flags": 0x1 | 0x8, "pitch": 0.0, "weapon": 1, "move": 0.0},
		{"x": -0.5, "team": 0, "flags": 0x1 | 0x8, "pitch": -15.0, "weapon": 4, "move": 3.0},
		{"x": 0.6, "team": 1, "flags": 0x1 | 0x2 | 0x8, "pitch": 10.0, "weapon": 2, "move": 0.0},
		{"x": 1.7, "team": 1, "flags": 0x1 | 0x8, "pitch": 0.0, "weapon": 5, "move": 3.5},
	]
	for p in poses:
		var op := TfpsOperator.new()
		root3d.add_child(op)
		_ops.append([op, p])


func _process(delta: float) -> bool:
	_frames += 1
	for e in _ops:
		var op: TfpsOperator = e[0]
		var p: Dictionary = e[1]
		var z := -float(p["move"]) * _frames * 0.016
		# Replicated positions are in Godot space; rotation.y = -yaw - 90.
		var sample := {"position": [p["x"], 0.0, z], "flags": p["flags"], "yaw": -_yaw - 90.0,
			"pitch": p["pitch"], "team": p["team"], "weapon": p["weapon"], "name": "Op"}
		op.apply(sample, 0.016)
		op.position.z = 0.0
	if _frames == 40:
		get_root().get_texture().get_image().save_png(_out)
		print("saved ", _out)
		quit()
	return false
