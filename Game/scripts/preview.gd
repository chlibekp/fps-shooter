## Renders a map from a camera position and saves a screenshot, then quits.
## godot --path Game res://scenes/preview.tscn -- --map harbor --cam x,y,z,yaw,pitch --out shot.png
## (camera in simulation coordinates: metres, Z up, yaw 0 = +X toward +Y)
extends Node3D

var _frames := 0
var _out := "user://preview.png"
var _wait := 90


func _ready() -> void:
	var args := _args()
	var map_name: String = args.get("map", "harbor")
	_out = args.get("out", _out)
	_wait = int(args.get("frames", "90"))
	var text := FileAccess.get_file_as_string("res://defs/maps/%s.json" % map_name)
	var map: Dictionary = JSON.parse_string(text)
	var world := TfpsWorld.new()
	add_child(world)
	world.build_from_json(map)
	var cam := Camera3D.new()
	cam.fov = 80.0
	add_child(cam)
	var c: PackedStringArray = args.get("cam", "0,0,1.7,0,0").split(",")
	var pos := Vector3(float(c[0]), float(c[2]), float(c[1]))
	cam.position = pos
	cam.rotation_degrees = Vector3(float(c[4]), -float(c[3]) - 90.0, 0.0)
	cam.make_current()


func _args() -> Dictionary:
	var out := {}
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		if a[i].begins_with("--") and i + 1 < a.size():
			out[a[i].substr(2)] = a[i + 1]
			i += 2
		else:
			i += 1
	return out


func _process(_delta: float) -> void:
	_frames += 1
	if _frames == _wait:
		get_viewport().get_texture().get_image().save_png(_out)
		print("saved ", _out)
		get_tree().quit()
