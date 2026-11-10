## Wraps the native client core (TfpsNative): loads definitions, connects,
## and once per frame sends the input and receives the client state.
class_name TfpsNet
extends RefCounted

const FORWARD := 1 << 0
const BACK := 1 << 1
const LEFT := 1 << 2
const RIGHT := 1 << 3
const JUMP := 1 << 4
const CROUCH := 1 << 5
const SPRINT := 1 << 6
const WALK := 1 << 7
const LEAN_LEFT := 1 << 8
const LEAN_RIGHT := 1 << 9
const AIM := 1 << 10
const FIRE := 1 << 11
const PRONE := 1 << 12
const RELOAD := 1 << 13
const USE := 1 << 14
const REMOTE_CONTROL := 0x80

# Entity flags (other players).
const F_ON_GROUND := 1 << 0
const F_CROUCHED := 1 << 1
const F_SPRINTING := 1 << 2
const F_ALIVE := 1 << 3
const F_PRONE := 1 << 4
const F_RELOADING := 1 << 5

var native: Object
var state: Dictionary = {}
var world: Dictionary = {}
var map_name := ""
var error := ""


func _init() -> void:
	if ClassDB.class_exists("TfpsNative"):
		native = ClassDB.instantiate("TfpsNative")
	else:
		error = "the native extension (Game/bin) did not load"


func request(d: Dictionary) -> Dictionary:
	if native == null:
		return {"ok": false, "error": error}
	var r = JSON.parse_string(native.request(JSON.stringify(d)))
	return r if r is Dictionary else {"ok": false, "error": "bad reply"}


## Loads a map and the weapon / gadget definitions from res://defs.
func load_map(name: String) -> bool:
	var map_text := FileAccess.get_file_as_string("res://defs/maps/%s.json" % name)
	if map_text.is_empty():
		error = "no map named '%s'" % name
		return false
	var r := request({"op": "load", "map": map_text,
		"weapons": FileAccess.get_file_as_string("res://defs/weapons.json"),
		"gadgets": FileAccess.get_file_as_string("res://defs/gadgets.json")})
	if not r.get("ok", false):
		error = r.get("error", "load failed")
		return false
	map_name = name
	world = request({"op": "world"})
	return true


func connect_to(address: String, player_name: String, join_token := "0", armor := 0, attachments := 0) -> bool:
	var r := request({"op": "connect", "address": address, "name": player_name, "join_token": join_token,
		"armor": armor, "attachments": attachments})
	if not r.get("ok", false):
		error = r.get("error", "connect failed")
		return false
	return true


func disconnect_from() -> void:
	request({"op": "disconnect"})
	state = {}


func frame(buttons: int, yaw: float, pitch: float, slot: int) -> Dictionary:
	state = request({"op": "frame", "buttons": buttons, "yaw": yaw, "pitch": pitch, "slot": slot})
	return state


func cells(ids: Array) -> Array:
	return request({"op": "cells", "ids": ids}).get("cells", [])


static func vec(a) -> Vector3:
	return Vector3(a[0], a[1], a[2]) if a is Array and a.size() == 3 else Vector3.ZERO


## Simulation yaw (0 = +X toward +Y) and pitch -> Godot camera rotation.
static func view_basis_rotation(yaw: float, pitch: float) -> Vector3:
	return Vector3(deg_to_rad(pitch), deg_to_rad(-yaw - 90.0), 0.0)
