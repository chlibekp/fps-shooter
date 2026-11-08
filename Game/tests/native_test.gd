## Headless test of the native extension against a running server:
## godot --headless --path Game --script res://tests/native_test.gd -- --server 127.0.0.1:7777
extends SceneTree

var native
var frames := 0
var start_feet := Vector3.ZERO
var connected_at := -1
var walk_started_ms := -1
var failures: Array[String] = []


func _initialize() -> void:
	var server := "127.0.0.1:7777"
	var a := OS.get_cmdline_user_args()
	for i in a.size() - 1:
		if a[i] == "--server":
			server = a[i + 1]
	if not ClassDB.class_exists("TfpsNative"):
		_fail("TfpsNative class not registered (extension did not load)")
		quit(1)
		return
	native = ClassDB.instantiate("TfpsNative")
	var load := _req({"op": "load",
		"map": FileAccess.get_file_as_string("res://defs/maps/test_arena.json"),
		"weapons": FileAccess.get_file_as_string("res://defs/weapons.json"),
		"gadgets": FileAccess.get_file_as_string("res://defs/gadgets.json")})
	_check(load.get("ok", false), "load: %s" % load)
	var world := _req({"op": "world"})
	_check(world.get("solids", []).size() > 10, "world has solids")
	var sounds := _req({"op": "sounds"})
	_check(sounds.get("cues", []).size() == 38, "38 sound cues")
	var pcm := Marshalls.base64_to_raw(_req({"op": "sound", "cue": 0, "variant": 0}).get("pcm", ""))
	_check(pcm.size() > 48000, "rifle shot renders (%d bytes)" % pcm.size())
	var c := _req({"op": "connect", "address": server, "name": "GodotTest", "join_token": "0"})
	_check(c.get("ok", false), "connect: %s" % c)


func _process(_delta: float) -> bool:
	frames += 1
	var walking := walk_started_ms >= 0
	var f := _req({"op": "frame", "buttons": 1 if walking else 0, "yaw": 90.0, "pitch": 0.0, "slot": 0})
	if connected_at < 0 and f.get("phase") == "connected":
		connected_at = frames
		print("connected as player ", f.get("player_id"))
	if connected_at >= 0 and frames - connected_at == 30:
		start_feet = _vec(f["feet"])
		walk_started_ms = Time.get_ticks_msec()
	if walk_started_ms >= 0 and Time.get_ticks_msec() - walk_started_ms >= 2000:
		walk_started_ms = -2
		var feet := _vec(f["feet"])
		var moved := feet - start_feet
		print("moved ", moved, " corrections ", f["stats"]["corrections"], " rtt ", f["stats"]["rtt_ms"])
		# Simulation yaw 90 = +Y, which is Godot +Z.
		_check(moved.z > 4.0 and absf(moved.x) < 0.2 and absf(moved.y) < 0.05, "walked along sim +Y = Godot +Z: %s" % moved)
		_check(f["stats"]["corrections"] <= 2, "prediction agrees with the server")
		_check(f["weapon"].get("name", "") != "", "has a weapon: %s" % f["weapon"].get("name", ""))
		_req({"op": "disconnect"})
		print("NATIVE TEST ", "PASSED" if failures.is_empty() else "FAILED")
		native = null
		quit(0 if failures.is_empty() else 1)
	if Time.get_ticks_msec() > 30000:
		_fail("never connected / finished (phase %s, reason %s)" % [f.get("phase"), f.get("reason")])
		native = null
		quit(1)
	return false


func _req(d: Dictionary) -> Dictionary:
	var r = JSON.parse_string(native.request(JSON.stringify(d)))
	return r if r is Dictionary else {}


func _vec(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])


func _check(ok: bool, what: String) -> void:
	if not ok:
		_fail(what)


func _fail(what: String) -> void:
	failures.append(what)
	printerr("FAILED: ", what)
